#!/usr/bin/env python3
"""Dry-run-first live corpus-client.v1 run against voice-lab's own fixture server.

Uses an explicit voice-lab checkout, or the pinned commit fetched from GitHub
when none is given (scripts/voice_lab/voice_lab_pin.py). Makes a CPython 3.12
venv under the ignored .build/voice-lab/live-contract/ with the API image's
hash-locked closure (requirements/vault-linux.txt), serves
``python -m cole_voice_lab.contract_fixture serve`` on loopback with a
throwaway TLS certificate, and drives the recorder's C++ sign-in code and HTTP
request API through each scenario. The fixture's tokens live only in the
disposable state directory and are never printed. Prints one JSON report.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from voice_lab_pin import PinnedSourceError, pinned_revision, resolve_source

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = "simulation_not_hardware_evidence"
SCOPE = "voice_lab_contract_fixture_loopback_tls"
DEFAULT_STATE = ROOT / ".build/voice-lab/live-contract"
DEFAULT_CLIENT = ROOT / ".build/host-dev/voice_lab_recorder/voice_lab_recorder_live_contract_client"
FIXTURE_MODULE = "cole_voice_lab.contract_fixture"
LOCKED_CLOSURE = "requirements/vault-linux.txt"
REQUIRED_PYTHON = (3, 12)
SCENARIO_ORDER = ("sign_in", "library", "intake_interrupted", "intake_resume", "decide", "outbox", "reuse_revokes")
READY_SECONDS = 60


class ContractError(RuntimeError):
    """A stable, secret-free failure code with a short detail."""

    def __init__(self, code: str, detail: str = "") -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def run(command: list[str], *, env: dict[str, str] | None = None, cwd: Path | None = None,
        timeout: float = 120) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True,
                          timeout=timeout, check=False)


def require(result: subprocess.CompletedProcess[str], code: str) -> str:
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-1:] or [""]
        raise ContractError(code, f"exit={result.returncode} {tail[0][:200]}")
    return result.stdout


def find_python(explicit: str | None) -> str:
    """CPython 3.12: the closure's wheels are cp312 only."""
    candidates = [explicit] if explicit else [os.environ.get("BHP_VOICE_LAB_PYTHON"),
                                              shutil.which("python3.12")]
    if not explicit and shutil.which("uv"):
        found = run(["uv", "python", "find", "3.12"], timeout=30)
        if found.returncode == 0:
            candidates.append(found.stdout.strip())
    for candidate in filter(None, candidates):
        probe = run([candidate, "-I", "-c", "import sys,platform;"
                     "print(sys.version_info[0],sys.version_info[1],platform.python_implementation())"],
                    timeout=30)
        if probe.returncode == 0 and probe.stdout.split() == ["3", "12", "CPython"]:
            return candidate
        if explicit:
            raise ContractError("PYTHON_VERSION_MISMATCH", "--python must be CPython 3.12")
    raise ContractError("PYTHON_312_MISSING", "pass --python or set BHP_VOICE_LAB_PYTHON")


class LiveContract:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.state = args.state_root.resolve()
        self.tls = self.state / "tls"
        self.fixture_root: Path | None = None
        self.panel_state = self.state / "panel"
        self.fixture: subprocess.Popen[str] | None = None
        self.report: dict[str, object] = {
            "schema_version": 1, "evidence": EVIDENCE, "evidence_scope": SCOPE,
            "started_at": utc_now(), "scenarios": [],
        }

    # --- environment -----------------------------------------------------------------
    def select_source(self) -> None:
        try:
            source, selection = resolve_source(self.args.voice_lab_source_dir)
        except PinnedSourceError as error:
            raise ContractError(error.code, error.detail) from error
        self.source = source.resolve()
        if not ((self.source / "contracts/corpus-client.v1/manifest.json").is_file()
                and (self.source / "src/cole_voice_lab/contract_fixture.py").is_file()
                and (self.source / LOCKED_CLOSURE).is_file()):
            raise ContractError("VOICE_LAB_SOURCE_INVALID", "missing the bundle, the fixture or the lock")
        head = require(run(["git", "-C", str(self.source), "rev-parse", "HEAD"]),
                       "VOICE_LAB_REVISION_UNREADABLE").strip()
        dirty = require(run(["git", "-C", str(self.source), "status", "--porcelain",
                             "--untracked-files=no"]), "VOICE_LAB_STATUS_UNREADABLE").strip()
        if dirty:
            raise ContractError("VOICE_LAB_CHECKOUT_DIRTY", "tracked changes present")
        self.report["voice_lab"] = {"revision": head, "source": selection,
                                    "matches_contract_pin": head == pinned_revision()}

    def make_venv(self) -> None:
        python = find_python(self.args.python)
        venv = self.state / "venv"
        interpreter = venv / "bin/python"
        if not interpreter.exists():
            require(run([python, "-I", "-m", "venv", str(venv)], timeout=300), "VENV_FAILED")
        require(run([str(interpreter), "-I", "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
                     "--no-deps", "--require-hashes", "--only-binary=:all:", "-r",
                     str(self.source / LOCKED_CLOSURE)], timeout=600), "LOCKED_CLOSURE_INSTALL_FAILED")
        purelib = require(run([str(interpreter), "-I", "-c",
                               "import sysconfig;print(sysconfig.get_paths()['purelib'])"]),
                          "VENV_UNREADABLE").strip()
        # The package runs from the checkout itself, never built or copied.
        (Path(purelib) / "bhp_voice_lab_source.pth").write_text(str(self.source / "src") + "\n",
                                                                 encoding="utf-8")
        self.python = interpreter
        self.report["python"] = require(run([str(interpreter), "-I", "-c",
                                             "import sys;print(sys.version.split()[0])"]),
                                        "VENV_UNREADABLE").strip()

    def make_certificate(self) -> None:
        if self.tls.exists():
            shutil.rmtree(self.tls)
        self.tls.mkdir(parents=True)
        os.chmod(self.tls, 0o700)
        self.certificate = self.tls / "fixture.crt"
        self.key = self.tls / "fixture.key"
        require(run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
                     "-nodes", "-days", "1", "-subj", "/CN=127.0.0.1",
                     "-addext", "subjectAltName=IP:127.0.0.1",
                     "-keyout", str(self.key), "-out", str(self.certificate)]),
                "TLS_CERTIFICATE_FAILED")

    def start_fixture(self) -> None:
        if self.panel_state.exists():
            shutil.rmtree(self.panel_state)
        self.panel_state.mkdir(parents=True)
        # The fixture's root goes in the temporary directory, not under .build/: on
        # btrfs, os.listdir() on a directory descriptor opened earlier misses entries
        # created since, and voice-lab's corpus store then refuses its own files
        # (CORPUS.STORE_UNSAFE). tmpfs and ext4 list them.
        self.fixture_root = Path(tempfile.mkdtemp(prefix="bhp-voice-lab-fixture-"))
        os.chmod(self.panel_state, 0o700)
        report = self.state / "fixture.json"
        report.unlink(missing_ok=True)
        env = {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
        log = open(self.state / "fixture.log", "w", encoding="utf-8")
        self.fixture = subprocess.Popen(
            [str(self.python), "-I", "-B", "-m", FIXTURE_MODULE, "serve", "--root", str(self.fixture_root),
             "--tls-cert", str(self.certificate), "--tls-key", str(self.key), "--report", str(report),
             "--interval", "1", "--approve-after", "1"],
            cwd=self.state, env=env, stdout=log, stderr=subprocess.STDOUT, text=True)
        deadline = time.monotonic() + READY_SECONDS
        while time.monotonic() < deadline:
            if self.fixture.poll() is not None:
                raise ContractError("FIXTURE_EXITED", f"exit={self.fixture.returncode}")
            if report.is_file():
                try:
                    self.endpoints = json.loads(report.read_text(encoding="utf-8"))
                except ValueError:
                    self.endpoints = None
                if self.endpoints and self.endpoints.get("contract_id") == "corpus-client.v1":
                    self.report["fixture"] = {key: self.endpoints.get(key) for key in
                                              ("contract_id", "client_id", "capture_profile")}
                    return
            time.sleep(0.25)
        raise ContractError("FIXTURE_READINESS_TIMEOUT")

    def stop_fixture(self) -> None:
        if self.fixture and self.fixture.poll() is None:
            self.fixture.send_signal(signal.SIGTERM)
            try:
                self.fixture.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.fixture.kill()
                self.fixture.wait(timeout=5)
        self.fixture = None

    # --- scenarios -------------------------------------------------------------------
    def client(self, scenario: str) -> dict[str, object]:
        command = [str(self.args.client), "--scenario", scenario, "--issuer", str(self.endpoints["issuer"]),
                   "--api-url", str(self.endpoints["api_url"]), "--ca-file", str(self.certificate),
                   "--state-directory", str(self.panel_state)]
        result = run(command, timeout=180)
        lines = [line for line in result.stdout.splitlines() if line.startswith("{")]
        if not lines:
            raise ContractError("CLIENT_REPORT_MISSING", f"{scenario} exit={result.returncode}")
        document = json.loads(lines[-1])
        document["exit_status"] = result.returncode
        document["passed"] = bool(document.get("ok")) and result.returncode == 0
        self.report["scenarios"].append(document)
        if not document["passed"]:
            failed = [name for name, ok in document.get("checks", {}).items() if not ok]
            raise ContractError("SCENARIO_FAILED", f"{scenario}: {failed[:4]}")
        return document.get("facts", {})

    def run_scenarios(self) -> None:
        for scenario in SCENARIO_ORDER:
            self.client(scenario)

    # --- lifecycle -------------------------------------------------------------------
    def teardown(self) -> None:
        self.stop_fixture()
        # The key and the fixture's tokens never outlive the run.
        shutil.rmtree(self.tls, ignore_errors=True)
        shutil.rmtree(self.panel_state, ignore_errors=True)
        if self.fixture_root and not self.args.keep_state:
            shutil.rmtree(self.fixture_root, ignore_errors=True)
        elif self.fixture_root:
            self.report["fixture_root"] = str(self.fixture_root)

    def execute(self) -> int:
        self.state.mkdir(parents=True, exist_ok=True)
        try:
            if not self.args.client.is_file():
                raise ContractError("CLIENT_MISSING", str(self.args.client))
            self.select_source()
            self.make_venv()
            self.make_certificate()
            self.start_fixture()
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
    parser.add_argument("--voice-lab-source-dir", type=Path,
                        help="explicit voice-lab checkout; default: fetch the pinned commit")
    parser.add_argument("--python", help="CPython 3.12 for the fixture's venv")
    parser.add_argument("--client", type=Path, default=DEFAULT_CLIENT)
    parser.add_argument("--state-root", type=Path, default=DEFAULT_STATE)
    parser.add_argument("--keep-state", action="store_true", help="keep the fixture's root")
    parser.add_argument("--publish", type=Path, help="also write the report here")
    args = parser.parse_args()
    if not args.execute:
        source = (str(args.voice_lab_source_dir) if args.voice_lab_source_dir
                  else f"pinned_fetch:{pinned_revision()}")
        print(json.dumps({"execute": False, "voice_lab_source_dir": source, "client": str(args.client),
                          "state_root": str(args.state_root), "fixture": FIXTURE_MODULE,
                          "locked_closure": LOCKED_CLOSURE, "python": "CPython 3.12",
                          "scenarios": list(SCENARIO_ORDER), "evidence": EVIDENCE,
                          "evidence_scope": SCOPE}))
        return 0
    return LiveContract(args).execute()


if __name__ == "__main__":
    sys.exit(main())
