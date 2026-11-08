"""bhp-report-inbox: the receiver and the agent's commands.

    serve          the HTTP receiver (the setup doc's systemd unit runs it)
    list           reports, newest first; --new lists only those nobody has taken yet
    show ID        a report's paths and closed fields, never its description or logs
    set-status ID STATUS [--pr N] [--reason TEXT]
    public-summary ID [--markdown]
    retention      deletes reports closed more than --days ago (90)

Exit codes: 0 done, 1 refused (with a REPORT_INBOX.* code on stderr), 2 usage.
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from pathlib import Path

from . import redact, server, validate
from .store import Store, StoreError

DEFAULT_ROOT = "/srv/house-panel-reports"


def _fail(code: str) -> int:
    print(json.dumps({"error_code": code}), file=sys.stderr)
    return 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="bhp-report-inbox")
    parser.add_argument("--root", default=DEFAULT_ROOT)
    commands = parser.add_subparsers(dest="command", required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--address", required=True)
    serve.add_argument("--port", type=int, default=8743)
    serve.add_argument("--allowed-tag", default="tag:house-panel")
    serve.add_argument("--tailscale", default="/usr/bin/tailscale")
    serve.add_argument("--cert")
    serve.add_argument("--key")
    serve.add_argument("--transcribe-command")
    serve.add_argument("--insecure-loopback-for-tests", action="store_true")
    listing = commands.add_parser("list")
    listing.add_argument("--new", action="store_true")
    show = commands.add_parser("show")
    show.add_argument("report_id")
    status = commands.add_parser("set-status")
    status.add_argument("report_id")
    status.add_argument("status", choices=[s for s in validate.STATUSES if s != "queued"])
    status.add_argument("--pr", type=int)
    status.add_argument("--reason")
    summary = commands.add_parser("public-summary")
    summary.add_argument("report_id")
    summary.add_argument("--markdown", action="store_true")
    retention = commands.add_parser("retention")
    retention.add_argument("--days", type=int, default=90)
    args = parser.parse_args(argv)
    root = Path(args.root)

    if args.command == "serve":
        if args.insecure_loopback_for_tests:
            if args.address != "127.0.0.1":
                parser.error("--insecure-loopback-for-tests binds 127.0.0.1 only")
            authenticate = lambda address: address == "127.0.0.1"  # noqa: E731
        else:
            if not (args.cert and args.key):
                parser.error("serve needs --cert and --key (tailscale cert)")
            authenticate = server.tailscale_whois_authenticator(args.tailscale, args.allowed_tag)
        server.serve(root, args.address, args.port, authenticate, Path(args.cert) if args.cert else None,
                     Path(args.key) if args.key else None, args.transcribe_command)
        return 0

    store = Store(root)
    try:
        if args.command == "list":
            rows = []
            for report_id in store.report_ids():
                manifest = store.manifest(report_id)
                state = store.status(report_id) or {}
                if args.new and state.get("status") != "received":
                    continue
                rows.append({"report_id": report_id, "created_at": manifest["created_at"], "kind": manifest["kind"],
                             "component": manifest["component"], "error_code": manifest["error_code"],
                             "status": state.get("status"), "pr": state.get("pr")})
            rows.sort(key=lambda row: row["created_at"], reverse=True)
            print(json.dumps(rows, indent=2))
        elif args.command == "show":
            manifest = store.manifest(args.report_id)
            directory = root / "reports" / args.report_id
            transcript = root / "transcripts" / f"{args.report_id}.txt"
            print(json.dumps({
                "report_id": args.report_id, "directory": str(directory), "kind": manifest["kind"],
                "component": manifest["component"], "error_code": manifest["error_code"],
                "location": manifest["location"], "screen": manifest["screen"], "build": manifest["build"],
                "files": [str(directory / f["name"]) for f in manifest["files"]],
                "has_description_text": manifest["description_text"] is not None,
                "transcript": str(transcript) if transcript.exists() else None,
                "status": store.status(args.report_id)}, indent=2))
        elif args.command == "set-status":
            store.set_status(args.report_id, args.status, args.pr, args.reason)
        elif args.command == "public-summary":
            manifest = store.manifest(args.report_id)
            record = store.fingerprint(manifest["fingerprint"])
            summary = redact.public_summary(manifest, record)
            if args.markdown:
                print(redact.issue_title(summary))
                print()
                print(redact.issue_body(summary), end="")
            else:
                print(json.dumps(summary, indent=2))
        elif args.command == "retention":
            print(json.dumps({"removed": store.apply_retention(datetime.datetime.now(datetime.timezone.utc),
                                                               args.days)}))
    except StoreError as error:
        return _fail(error.code)
    except redact.RedactionError as error:
        return _fail(f"REPORT_INBOX.REDACTION_REFUSED.{error.field.upper().replace('.', '_')}")
    except (FileNotFoundError, KeyError):
        return _fail("REPORT_INBOX.UNKNOWN_REPORT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
