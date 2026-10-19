#!/usr/bin/env python3
"""Resolve the Lexinet source for host-side contract checks.

An explicit ``--lexinet-source-dir`` checkout is used as given. Without one,
the pinned commit is fetched from the public GitHub repository into an ignored
cache under ``.build/lexinet/source/`` and checked before use. The pin is the
``repository_base_revision`` in the checked-in study.v1 provenance; the content
digests in that file stay the integrity check. No tool infers a sibling or
workstation checkout.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY_URL = "https://github.com/scshafe/lexinet"
PROVENANCE = ROOT / "vocabulary_panel/tests/fixtures/study_v1/contract-provenance.json"
DEFAULT_CACHE_ROOT = ROOT / ".build/lexinet/source"
FETCH_TIMEOUT_SECONDS = 300
FULL_SHA = re.compile(r"[0-9a-f]{40}")


class PinnedSourceError(RuntimeError):
    """A stable, content-free failure code with a short detail."""

    def __init__(self, code: str, detail: str = "") -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


def pinned_revision(provenance: Path = PROVENANCE) -> str:
    try:
        source = json.loads(provenance.read_text(encoding="utf-8"))["source"]
        revision = source["repository_base_revision"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise PinnedSourceError("LEXINET_PIN_UNREADABLE", "contract provenance") from error
    if not isinstance(revision, str) or not FULL_SHA.fullmatch(revision):
        raise PinnedSourceError("LEXINET_PIN_INVALID", "repository_base_revision is not a full SHA")
    return revision


def _git(arguments: list[str], cwd: Path, timeout: float = 30) -> str:
    environment = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    try:
        result = subprocess.run(["git", *arguments], cwd=cwd, env=environment, text=True,
                                capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        raise PinnedSourceError("LEXINET_GIT_FAILED", arguments[0]) from error
    if result.returncode != 0:
        raise PinnedSourceError("LEXINET_GIT_FAILED", f"{arguments[0]} exit={result.returncode}")
    return result.stdout.strip()


def verify_pinned_checkout(checkout: Path, revision: str) -> None:
    """Require HEAD at the pin with no tracked change."""
    try:
        head = _git(["rev-parse", "--verify", "HEAD"], checkout)
        top = Path(_git(["rev-parse", "--show-toplevel"], checkout)).resolve()
        dirty = _git(["status", "--porcelain", "--untracked-files=no"], checkout)
    except PinnedSourceError as error:
        raise PinnedSourceError("LEXINET_CACHE_INVALID", str(checkout)) from error
    if top != checkout.resolve() or head != revision or dirty:
        raise PinnedSourceError("LEXINET_CACHE_INVALID",
                                f"{checkout} is not a clean checkout of {revision}; delete it")


def fetch_pinned_source(revision: str, cache_root: Path = DEFAULT_CACHE_ROOT,
                        repository_url: str = REPOSITORY_URL) -> Path:
    """Return a clean checkout of revision, fetching it once into cache_root."""
    if not FULL_SHA.fullmatch(revision):
        raise PinnedSourceError("LEXINET_PIN_INVALID", "revision is not a full SHA")
    checkout = cache_root / revision
    if checkout.exists():
        verify_pinned_checkout(checkout, revision)
        return checkout
    cache_root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{revision[:12]}-", dir=cache_root))
    try:
        _git(["init", "-q"], staging)
        try:
            _git(["fetch", "-q", "--depth", "1", "--no-tags", repository_url, revision], staging,
                 timeout=FETCH_TIMEOUT_SECONDS)
        except PinnedSourceError as error:
            raise PinnedSourceError("LEXINET_PIN_FETCH_FAILED", repository_url) from error
        _git(["-c", "advice.detachedHead=false", "checkout", "-q", "--detach", "FETCH_HEAD"],
             staging)
        verify_pinned_checkout(staging, revision)
        try:
            staging.rename(checkout)
        except OSError:
            # A concurrent run filled the cache first; use its checkout.
            if not checkout.exists():
                raise PinnedSourceError("LEXINET_CACHE_INVALID", str(checkout)) from None
            shutil.rmtree(staging, ignore_errors=True)
            verify_pinned_checkout(checkout, revision)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return checkout


def resolve_source(explicit: Path | None) -> tuple[Path, str]:
    """Return the source directory and how it was selected."""
    if explicit is not None:
        return explicit, "explicit"
    return fetch_pinned_source(pinned_revision()), "pinned_fetch"
