#!/usr/bin/env python3
"""Dry-run-first live study.v1 contract run against a disposable real Lexinet.

Builds Lexinet from an explicit checkout, serves it on loopback with a
disposable PostgreSQL cluster and a disposable bearer under ignored
.build/lexinet/, seeds a synthetic corpus through Lexinet's JSON CLI, and drives
the panel's C++ study.v1 client and durable reducer through every contract
scenario. The bearer is test-only: it is never written to panel configuration,
images or the virtual panel, and never printed. Prints one JSON report.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = "simulation_not_hardware_evidence"
SCOPE = "real_lexinet_loopback_disposable_postgres"
DEFAULT_STATE = ROOT / ".build/lexinet/live-contract"
DEFAULT_CLIENT = (
    ROOT / ".build/host-dev/vocabulary_panel/vocabulary_panel_study_live_lexinet_contract_client"
)
PROFILE_REF = "panel-live-contract"
TOPIC_SLUG = "panel-live-contract-words"
CRASH_EXIT = 86
# Synthetic, non-personal corpus: (name, definition, example).
CORPUS = (
    ("Apricity", "The warmth of the sun in winter.", "We stood in the apricity by the fence."),
    ("Petrichor", "The earthy scent after rain on dry soil.", "Petrichor drifted through the window."),
    ("Sonder", "The realization that each passerby has a vivid inner life.",
     "Sonder arrived on the crowded train."),
)
SCENARIO_ORDER = (
    "create_resume", "study_and_sync", "auth_failure", "stop_lexinet", "offline_queue",
    "start_lexinet", "recover", "crash_after_send", "recover", "restart_lexinet",
    "resume_after_server_restart", "exhaust_and_successor", "atomic_rejection",
)


class ContractError(RuntimeError):
    """A stable, secret-free failure code with a short detail."""

    def __init__(self, code: str, detail: str = "") -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def run(command: list[str], *, env: dict[str, str] | None = None, cwd: Path | None = None,
        timeout: float = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True,
                          timeout=timeout, check=False)


def require(result: subprocess.CompletedProcess[str], code: str) -> str:
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-1:] or [""]
        raise ContractError(code, f"exit={result.returncode} {tail[0][:200]}")
    return result.stdout


def write_private(path: Path, text: str) -> None:
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
        handle.write(text)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def postgres_bin_dir(explicit: Path | None, major: str) -> Path:
    if explicit:
        return explicit
    # Versioned server layouts first (Debian/Ubuntu, Homebrew), then PATH.
    for pattern in (f"/usr/lib/postgresql/{major}/bin", f"/opt/homebrew/opt/postgresql@{major}/bin",
                    f"/usr/local/opt/postgresql@{major}/bin"):
        if (Path(pattern) / "initdb").exists():
            return Path(pattern)
    pg_config = shutil.which("pg_config")
    if pg_config:
        candidate = Path(require(run([pg_config, "--bindir"]), "POSTGRES_BINDIR_UNREADABLE").strip())
        if (candidate / "initdb").exists():
            return candidate
    initdb = shutil.which("initdb")
    if initdb:
        return Path(initdb).parent
    raise ContractError("POSTGRES_BINARIES_MISSING", "pass --postgres-bin-dir")


class LiveContract:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.source = args.lexinet_source_dir.resolve()
        self.state = args.state_root.resolve()
        self.secrets = self.state / "secrets"
        self.lexinet_process: subprocess.Popen[str] | None = None
        self.lexinet_starts = 0
        self.report: dict[str, object] = {
            "schema_version": 1, "evidence": EVIDENCE, "evidence_scope": SCOPE,
            "started_at": utc_now(), "scenarios": [], "cross_checks": {},
        }
        self.facts: dict[str, dict[str, object]] = {}

    # --- environment -----------------------------------------------------------------
    def verify_checkout(self) -> None:
        if not (self.source / "go.mod").is_file() or not (self.source / "contracts/study.v1").is_dir():
            raise ContractError("LEXINET_SOURCE_INVALID", "missing go.mod or contracts/study.v1")
        head = require(run(["git", "-C", str(self.source), "rev-parse", "HEAD"]),
                       "LEXINET_REVISION_UNREADABLE").strip()
        dirty = require(run(["git", "-C", str(self.source), "status", "--porcelain",
                             "--untracked-files=no"]), "LEXINET_STATUS_UNREADABLE").strip()
        if dirty:
            raise ContractError("LEXINET_CHECKOUT_DIRTY", "tracked changes present")
        self.report["lexinet"] = {"revision": head}

    def build_lexinet(self) -> None:
        binary = self.state / "bin/lexinet"
        binary.parent.mkdir(parents=True, exist_ok=True)
        if self.args.lexinet_binary:
            if self.args.lexinet_binary.resolve() != binary.resolve():
                shutil.copy2(self.args.lexinet_binary, binary)
            self.report["lexinet"]["prebuilt_binary"] = True
        else:
            go = shutil.which("go")
            if not go:
                raise ContractError("GO_MISSING", "Go matching Lexinet's go.mod is required")
            require(run([go, "build", "-trimpath", "-o", str(binary), "./cmd/lexinet"],
                        cwd=self.source, timeout=1800), "LEXINET_BUILD_FAILED")
            self.report["lexinet"]["go_version"] = require(run([go, "version"]),
                                                           "GO_VERSION_UNREADABLE").strip()
        self.binary = binary
        self.report["lexinet"]["binary_sha256"] = sha256_file(binary)

    def init_postgres(self) -> None:
        bindir = postgres_bin_dir(self.args.postgres_bin_dir, self.args.postgres_major)
        version = require(run([str(bindir / "postgres"), "--version"]), "POSTGRES_VERSION_UNREADABLE").strip()
        major = version.split()[-1].split(".")[0]
        if major != self.args.postgres_major:
            raise ContractError("POSTGRES_MAJOR_MISMATCH", f"found {major}")
        self.report["postgres"] = {"version": version}
        data = self.state / "pg/data"
        if data.exists():
            shutil.rmtree(data)
        data.parent.mkdir(parents=True, exist_ok=True)
        # Short socket directory: Unix socket paths are length-limited on macOS.
        self.pg_socket = Path(tempfile.mkdtemp(prefix="bhp-pg-"))
        self.pg_port = free_port()
        self.pg = bindir
        require(run([str(bindir / "initdb"), "-D", str(data), "-U", "postgres", "--auth=trust",
                     "-E", "UTF8", "--no-instructions"]), "POSTGRES_INITDB_FAILED")
        options = f"-p {self.pg_port} -k {self.pg_socket} -c listen_addresses=127.0.0.1"
        require(run([str(bindir / "pg_ctl"), "-D", str(data), "-o", options, "-l",
                     str(self.state / "pg/postgres.log"), "-w", "start"]), "POSTGRES_START_FAILED")
        self.pg_data = data
        connection = ["-h", "127.0.0.1", "-p", str(self.pg_port), "-U", "postgres"]
        require(run([str(bindir / "createuser"), *connection, "lexinet"]), "POSTGRES_ROLE_FAILED")
        require(run([str(bindir / "createdb"), *connection, "-O", "lexinet", "lexinet"]),
                "POSTGRES_DATABASE_FAILED")
        self.dsn = f"postgres://lexinet@127.0.0.1:{self.pg_port}/lexinet?sslmode=disable"

    def write_secrets(self) -> None:
        self.secrets.mkdir(parents=True, exist_ok=True)
        os.chmod(self.secrets, 0o700)
        bearer = secrets.token_urlsafe(32)
        self.bearer_file = self.secrets / "study-bearer"
        self.wrong_bearer_file = self.secrets / "wrong-study-bearer"
        write_private(self.bearer_file, bearer + "\n")
        write_private(self.wrong_bearer_file, secrets.token_urlsafe(32) + "\n")
        self.lexinet_port = free_port()
        self.endpoint = f"http://127.0.0.1:{self.lexinet_port}"
        self.config = self.secrets / "lexinet.toml"
        write_private(self.config, "\n".join([
            "[server]", f'url = "{self.endpoint}"',
            "[service]", f'listen_loopback = "127.0.0.1:{self.lexinet_port}"',
            f'auth_token = "{bearer}"', "tailnet_enabled = false",
            "[service.database]", f'dsn = "{self.dsn}"', ""]))

    def lexinet_env(self) -> dict[str, str]:
        env = dict(os.environ)
        env["LEXINET_CONFIG"] = str(self.config)
        return env

    def cli(self, *arguments: str, code: str) -> dict[str, object]:
        output = require(run([str(self.binary), "--endpoint", self.endpoint, "--token-file",
                              str(self.bearer_file), "--json", "--compact", *arguments],
                             env=self.lexinet_env()), code)
        return json.loads(output)

    def migrate(self) -> None:
        require(run([str(self.binary), "migrate", "--json"], env=self.lexinet_env()),
                "LEXINET_MIGRATE_FAILED")

    def start_lexinet(self) -> None:
        self.lexinet_starts += 1
        log = open(self.state / f"lexinet-serve-{self.lexinet_starts}.log", "w", encoding="utf-8")
        self.lexinet_process = subprocess.Popen(
            [str(self.binary), "serve", "--tailnet", "off"], env=self.lexinet_env(),
            stdout=log, stderr=subprocess.STDOUT, text=True)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.lexinet_process.poll() is not None:
                raise ContractError("LEXINET_EXITED", f"exit={self.lexinet_process.returncode}")
            probe = run([str(self.binary), "--endpoint", self.endpoint, "--token-file",
                         str(self.bearer_file), "ping", "--json"],
                        env=self.lexinet_env(), timeout=5)
            if probe.returncode == 0:
                return
            time.sleep(0.25)
        raise ContractError("LEXINET_READINESS_TIMEOUT")

    def stop_lexinet(self) -> None:
        if self.lexinet_process and self.lexinet_process.poll() is None:
            self.lexinet_process.send_signal(signal.SIGTERM)
            try:
                self.lexinet_process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.lexinet_process.kill()
                self.lexinet_process.wait(timeout=5)
        self.lexinet_process = None

    def seed(self) -> None:
        self.cli("topic", "add", "Panel live contract words", "--slug", TOPIC_SLUG,
                 "--description", "Synthetic words for the panel live contract", code="SEED_TOPIC_FAILED")
        for name, definition, example in CORPUS:
            slug = name.lower()
            self.cli("idea", "add", name, "--slug", slug, "--summary", definition,
                     "--definition", definition, "--example", example, code="SEED_IDEA_FAILED")
            self.cli("topic", "assign", slug, TOPIC_SLUG, code="SEED_ASSIGN_FAILED")
        self.report["corpus"] = {"topic": TOPIC_SLUG, "profile_ref": PROFILE_REF,
                                 "cards": len(CORPUS), "synthetic": True}

    # --- scenarios -------------------------------------------------------------------
    def client(self, scenario: str, *, credential: Path | None = None, expect_session: str = "",
               expect_sync_id: str = "", expected_exit: int = 0) -> dict[str, object]:
        command = [str(self.args.client), "--scenario", scenario, "--base-url", self.endpoint,
                   "--credential-file", str(credential or self.bearer_file),
                   "--state-directory", str(self.state / "panel-study"),
                   "--profile-ref", PROFILE_REF, "--topic-ref", TOPIC_SLUG]
        if expect_session:
            command += ["--expect-session", expect_session]
        if expect_sync_id:
            command += ["--expect-sync-id", expect_sync_id]
        result = run(command, timeout=120)
        lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
        if not lines:
            raise ContractError("CLIENT_REPORT_MISSING", f"{scenario} exit={result.returncode}")
        document = json.loads(lines[-1])
        document["exit_status"] = result.returncode
        document["passed"] = bool(document.get("ok")) and result.returncode == expected_exit
        self.report["scenarios"].append(document)
        self.facts[scenario] = document.get("facts", {})
        if not document["passed"]:
            failed = [name for name, ok in document.get("checks", {}).items() if not ok]
            raise ContractError("SCENARIO_FAILED", f"{scenario}: {failed[:4]}")
        return document["facts"]

    def run_scenarios(self) -> None:
        created = self.client("create_resume")
        session = str(created["session_id"])
        self.client("study_and_sync", expect_session=session)
        auth = self.client("auth_failure", credential=self.wrong_bearer_file)
        self.stop_lexinet()
        offline = self.client("offline_queue", expect_sync_id=str(auth["pending_sync_id"]))
        self.start_lexinet()
        self.client("recover", expect_sync_id=str(offline["pending_sync_id"]))
        crashed = self.client("crash_after_send", expected_exit=CRASH_EXIT)
        replayed = self.client("recover", expect_sync_id=str(crashed["pending_sync_id"]))
        self.report["cross_checks"]["crash_replay_identical_flush_receipt"] = (
            replayed.get("recover.flush_receipt_sha256_0") == crashed.get("flush_receipt_sha256"))
        self.stop_lexinet()
        self.start_lexinet()
        self.client("resume_after_server_restart", expect_session=session)
        successor = self.client("exhaust_and_successor")
        self.client("atomic_rejection")
        state = self.cli("study", "state", str(successor["predecessor_session_id"]),
                         "--after-position", "-1", "--limit", "1", code="STATE_READ_FAILED")
        self.report["cross_checks"]["server_links_predecessor_to_successor"] = (
            state.get("successor_session_id") == successor["successor_session_id"])
        self.report["cross_checks"]["predecessor_not_active"] = state.get("lifecycle") != "active"
        failed = [name for name, ok in self.report["cross_checks"].items() if not ok]
        if failed:
            raise ContractError("CROSS_CHECK_FAILED", ",".join(failed))

    # --- lifecycle -------------------------------------------------------------------
    def teardown(self) -> None:
        self.stop_lexinet()
        if getattr(self, "pg_data", None):
            run([str(self.pg / "pg_ctl"), "-D", str(self.pg_data), "-m", "fast", "-w", "stop"])
        if getattr(self, "pg_socket", None):
            shutil.rmtree(self.pg_socket, ignore_errors=True)
        # Secrets never outlive the run; the database is disposable unless kept.
        shutil.rmtree(self.secrets, ignore_errors=True)
        if not self.args.keep_state:
            shutil.rmtree(self.state / "pg", ignore_errors=True)
            shutil.rmtree(self.state / "panel-study", ignore_errors=True)

    def execute(self) -> int:
        if self.state.exists() and (self.state / "panel-study").exists():
            shutil.rmtree(self.state / "panel-study")
        self.state.mkdir(parents=True, exist_ok=True)
        try:
            if not self.args.client.is_file():
                raise ContractError("CLIENT_MISSING", str(self.args.client))
            self.verify_checkout()
            self.build_lexinet()
            self.init_postgres()
            self.write_secrets()
            self.migrate()
            self.start_lexinet()
            self.seed()
            self.run_scenarios()
            self.report["ok"] = True
        except (ContractError, subprocess.TimeoutExpired, OSError, ValueError) as error:
            self.report["ok"] = False
            self.report["error"] = {"code": getattr(error, "code", type(error).__name__),
                                    "detail": getattr(error, "detail", str(error))[:300]}
        finally:
            self.teardown()
            self.report["finished_at"] = utc_now()
        text = json.dumps(self.report, indent=2) + "\n"
        (self.state / "report.json").write_text(text, encoding="utf-8")
        if self.args.publish:
            self.args.publish.write_text(text, encoding="utf-8")
        print(json.dumps(self.report))
        return 0 if self.report["ok"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--execute", action="store_true", help="run; default prints the plan")
    parser.add_argument("--lexinet-source-dir", type=Path, required=True,
                        help="explicit Lexinet checkout (never inferred)")
    parser.add_argument("--lexinet-binary", type=Path, help="prebuilt lexinet for this checkout")
    parser.add_argument("--client", type=Path, default=DEFAULT_CLIENT)
    parser.add_argument("--postgres-bin-dir", type=Path)
    parser.add_argument("--postgres-major", default="16", help="Lexinet deploys postgres:16")
    parser.add_argument("--state-root", type=Path, default=DEFAULT_STATE)
    parser.add_argument("--keep-state", action="store_true")
    parser.add_argument("--publish", type=Path, help="also write the report here")
    args = parser.parse_args()
    if not args.execute:
        print(json.dumps({"execute": False, "lexinet_source_dir": str(args.lexinet_source_dir),
                          "client": str(args.client), "state_root": str(args.state_root),
                          "profile_ref": PROFILE_REF, "topic": TOPIC_SLUG,
                          "scenarios": list(SCENARIO_ORDER), "evidence": EVIDENCE,
                          "evidence_scope": SCOPE}))
        return 0
    return LiveContract(args).execute()


if __name__ == "__main__":
    sys.exit(main())
