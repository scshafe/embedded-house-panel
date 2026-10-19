#!/usr/bin/env python3
"""Resolve a pinned contract source for host-side contract checks.

@canonical contract_pinned_source -- one mechanism for every pinned contract.

A contract repository is pinned by a full commit SHA kept in a checked-in
provenance file (``source.repository_base_revision``), next to the content
digests that stay the integrity check. An explicit source checkout is used as
given. Without one, the pinned commit is fetched shallowly from the
repository into an ignored cache directory and checked before use: HEAD at the
pin, its own top level, no tracked change. No tool infers a sibling or
workstation checkout. Error codes carry the contract's name as a prefix
(``LEXINET_PIN_INVALID``), so each contract keeps its own codes.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

FETCH_TIMEOUT_SECONDS = 300
FULL_SHA = re.compile(r"[0-9a-f]{40}")
NAME = re.compile(r"[A-Z][A-Z0-9_]*")


class PinnedSourceError(RuntimeError):
    """A stable, content-free failure code with a short detail."""

    def __init__(self, code: str, detail: str = "") -> None:
        super().__init__(f"{code}: {detail}")
        self.code = code
        self.detail = detail


@dataclass(frozen=True)
class ContractPin:
    """Where one contract's pin lives and where its source is cached."""

    name: str
    repository_url: str
    provenance: Path
    cache_root: Path

    def __post_init__(self) -> None:
        if not NAME.fullmatch(self.name):
            raise ValueError("contract name must be upper-case letters, digits and underscores")

    def error(self, suffix: str, detail: str = "") -> PinnedSourceError:
        return PinnedSourceError(f"{self.name}_{suffix}", detail)


def pinned_revision(pin: ContractPin) -> str:
    try:
        source = json.loads(pin.provenance.read_text(encoding="utf-8"))["source"]
        revision = source["repository_base_revision"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise pin.error("PIN_UNREADABLE", "contract provenance") from error
    if not isinstance(revision, str) or not FULL_SHA.fullmatch(revision):
        raise pin.error("PIN_INVALID", "repository_base_revision is not a full SHA")
    return revision


def _git(pin: ContractPin, arguments: list[str], cwd: Path, timeout: float = 30) -> str:
    environment = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    try:
        result = subprocess.run(["git", *arguments], cwd=cwd, env=environment, text=True,
                                capture_output=True, timeout=timeout, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        raise pin.error("GIT_FAILED", arguments[0]) from error
    if result.returncode != 0:
        raise pin.error("GIT_FAILED", f"{arguments[0]} exit={result.returncode}")
    return result.stdout.strip()


def verify_pinned_checkout(pin: ContractPin, checkout: Path, revision: str) -> None:
    """Require HEAD at the pin with no tracked change."""
    try:
        head = _git(pin, ["rev-parse", "--verify", "HEAD"], checkout)
        top = Path(_git(pin, ["rev-parse", "--show-toplevel"], checkout)).resolve()
        dirty = _git(pin, ["status", "--porcelain", "--untracked-files=no"], checkout)
    except PinnedSourceError as error:
        raise pin.error("CACHE_INVALID", str(checkout)) from error
    if top != checkout.resolve() or head != revision or dirty:
        raise pin.error("CACHE_INVALID", f"{checkout} is not a clean checkout of {revision}; delete it")


def fetch_pinned_source(pin: ContractPin, revision: str) -> Path:
    """Return a clean checkout of revision, fetching it once into the pin's cache."""
    if not FULL_SHA.fullmatch(revision):
        raise pin.error("PIN_INVALID", "revision is not a full SHA")
    checkout = pin.cache_root / revision
    if checkout.exists():
        verify_pinned_checkout(pin, checkout, revision)
        return checkout
    pin.cache_root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{revision[:12]}-", dir=pin.cache_root))
    try:
        _git(pin, ["init", "-q"], staging)
        try:
            _git(pin, ["fetch", "-q", "--depth", "1", "--no-tags", pin.repository_url, revision], staging,
                 timeout=FETCH_TIMEOUT_SECONDS)
        except PinnedSourceError as error:
            raise pin.error("PIN_FETCH_FAILED", pin.repository_url) from error
        _git(pin, ["-c", "advice.detachedHead=false", "checkout", "-q", "--detach", "FETCH_HEAD"], staging)
        verify_pinned_checkout(pin, staging, revision)
        try:
            staging.rename(checkout)
        except OSError:
            # A concurrent run filled the cache first; use its checkout.
            if not checkout.exists():
                raise pin.error("CACHE_INVALID", str(checkout)) from None
            shutil.rmtree(staging, ignore_errors=True)
            verify_pinned_checkout(pin, checkout, revision)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return checkout


def resolve_source(pin: ContractPin, explicit: Path | None) -> tuple[Path, str]:
    """Return the source directory and how it was selected."""
    if explicit is not None:
        return explicit, "explicit"
    return fetch_pinned_source(pin, pinned_revision(pin)), "pinned_fetch"
