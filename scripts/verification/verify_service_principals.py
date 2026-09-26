#!/usr/bin/env python3
"""Fail closed when the image's principals or device rules drift from the matrix.

Reads system_image/service-principals.json and checks the Buildroot external
tree statically: package USERS tables, init scripts, the mdev rewrite in
post-build.sh and the supervisor restart limits. This is packaging-contract
evidence only; the Mac-local image boot proves the running layout.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--root"
            else Path(__file__).resolve().parents[2]).resolve()
MATRIX = ROOT / "system_image/service-principals.json"
EXTERNAL = ROOT / "system_image/buildroot_external"
BOARD = EXTERNAL / "board/beaglebone-house-panel"
INIT = BOARD / "rootfs-overlay/etc/init.d"
POST_BUILD = BOARD / "post-build.sh"
USERS_BLOCK = re.compile(r"^define ([A-Z0-9_]+)_USERS\n(.*?)^endef", re.M | re.S)


class MatrixError(Exception):
    def __init__(self, code: str, detail: str) -> None:
        super().__init__(f"SERVICE_PRINCIPALS.{code}: {detail}")
        self.code = code


def fail(code: str, detail: str) -> None:
    raise MatrixError(code, detail)


def package_users() -> dict[str, dict[str, object]]:
    """Map each Buildroot-created user to its package and parsed fields."""
    users: dict[str, dict[str, object]] = {}
    for makefile in sorted(EXTERNAL.glob("package/*/*.mk")):
        for match in USERS_BLOCK.finditer(makefile.read_text(encoding="utf-8")):
            for line in match.group(2).splitlines():
                fields = line.split(maxsplit=8)
                if not fields:
                    continue
                if len(fields) < 9:
                    fail("USERS_LINE_INVALID", f"{makefile.name}: {line.strip()}")
                name, uid, group, gid, _password, _home, _shell, groups, _comment = fields
                if name in users:
                    fail("USER_DEFINED_TWICE", name)
                users[name] = {
                    "package": makefile.parent.name, "uid": int(uid), "group": group,
                    "gid": int(gid), "groups": [] if groups == "-" else groups.split(","),
                }
    return users


def main() -> int:
    matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
    if matrix.get("schema_version") != 1 or matrix.get("canonical") != "service-principal-device-matrix":
        fail("SCHEMA_INVALID", str(MATRIX))
    forbidden = set(matrix["forbidden_groups"])
    principals = matrix["principals"]
    installed = [p for p in principals if p["installed"]]
    reserved = [p for p in principals if not p["installed"]]
    users = package_users()

    # Distinct non-root identities across installed and reserved roles.
    seen: dict[int, str] = {}
    for principal in principals:
        uid = principal["uid"] if principal["installed"] else principal["reserved_uid"]
        if uid == 0:
            continue
        if uid in seen:
            fail("UID_SHARED", f"{principal['role']} and {seen[uid]} share {uid}")
        seen[uid] = principal["role"]

    display_owners = {p["user"] for p in installed if "display" in p["owns"]}
    for principal in installed:
        script = INIT / principal["service"]
        if not script.is_file():
            fail("SERVICE_SCRIPT_MISSING", principal["service"])
        if principal["user"] == "root":
            continue
        entry = users.get(principal["user"])
        if entry is None:
            fail("PRINCIPAL_NOT_CREATED", principal["user"])
        expected = {"package": principal["package"], "uid": principal["uid"],
                    "group": principal["group"], "gid": principal["gid"],
                    "groups": principal["supplementary_groups"]}
        if entry != expected:
            fail("PRINCIPAL_MISMATCH", f"{principal['user']}: {entry} != {expected}")

    installed_users = {p["user"] for p in installed}
    for name, entry in users.items():
        if name not in installed_users:
            fail("UNLISTED_PRINCIPAL", f"{name} from {entry['package']}")
        bad = forbidden.intersection(entry["groups"])
        if bad:
            fail("FORBIDDEN_GROUP", f"{name}: {sorted(bad)}")
        if "video" in entry["groups"] and name not in display_owners:
            fail("DISPLAY_GRANT_UNLISTED", name)

    init_text = {path.name: path.read_text(encoding="utf-8") for path in INIT.iterdir()}
    for principal in reserved:
        if principal["reserved_user"] in users:
            fail("RESERVED_PRINCIPAL_INSTALLED", principal["reserved_user"])
        for name, text in init_text.items():
            if principal["reserved_user"] in text:
                fail("RESERVED_PRINCIPAL_IN_SERVICE", f"{principal['reserved_user']} in {name}")
    for retired in matrix["retired"]:
        if (INIT / retired["service"]).exists() or retired["user"] in users:
            fail("RETIRED_PRINCIPAL_PRESENT", retired["role"])

    post_build = POST_BUILD.read_text(encoding="utf-8")
    for device in matrix["devices"]:
        rule = device.get("mdev")
        if rule and f'print "{rule.replace(chr(9), chr(92) + "t")}"' not in post_build:
            fail("MDEV_RULE_MISSING", rule.replace("\t", " "))
        if device["class"] == "sound" and (
                "$1 ~ /^snd\\// {" not in post_build or 'print $1 "\\troot:root 600"' not in post_build):
            fail("SOUND_RULE_MISSING", "snd nodes must be rewritten root:root 600")
        for holder in device.get("holders", []) + device.get("writers", []):
            if holder not in {p["role"] for p in installed}:
                fail("DEVICE_HOLDER_NOT_INSTALLED", f"{device['class']}: {holder}")

    supervisor = next(p for p in installed if p["role"] == "supervisor")
    s70 = init_text[supervisor["service"]]
    for key, value in (("RESTART_LIMIT", supervisor["restart"]["attempts"]),
                       ("RESTART_BACKOFF_SECONDS", supervisor["restart"]["backoff_seconds"])):
        if f"BHP_PANEL_SUPERVISOR_{key}={value}\n" not in s70:
            fail("SUPERVISOR_RESTART_MISMATCH", key)

    print(json.dumps({"service_principals": "verified", "installed": len(installed),
                      "reserved": len(reserved), "retired": len(matrix["retired"]),
                      "created_users": sorted(users), "evidence": matrix["evidence"]}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except MatrixError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
