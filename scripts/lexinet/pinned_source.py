#!/usr/bin/env python3
"""Resolve the Lexinet source for host-side contract checks.

The lexinet configuration of ``scripts/contracts/contract_pin.py``: the pin
is the ``repository_base_revision`` in the checked-in study.v1 provenance, the
cache is ``.build/lexinet/source/``, and error codes are ``LEXINET_*``. An
explicit ``--lexinet-source-dir`` checkout is used as given.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/contracts"))

import contract_pin as _contract  # noqa: E402

from contract_pin import ContractPin, PinnedSourceError  # noqa: E402,F401

REPOSITORY_URL = "https://github.com/scshafe/lexinet"
PROVENANCE = ROOT / "vocabulary_panel/tests/fixtures/study_v1/contract-provenance.json"
DEFAULT_CACHE_ROOT = ROOT / ".build/lexinet/source"
FULL_SHA = _contract.FULL_SHA


def _pin(provenance: Path = PROVENANCE, cache_root: Path = DEFAULT_CACHE_ROOT,
         repository_url: str = REPOSITORY_URL) -> ContractPin:
    return ContractPin("LEXINET", repository_url, provenance, cache_root)


def pinned_revision(provenance: Path = PROVENANCE) -> str:
    return _contract.pinned_revision(_pin(provenance))


def verify_pinned_checkout(checkout: Path, revision: str) -> None:
    _contract.verify_pinned_checkout(_pin(), checkout, revision)


def fetch_pinned_source(revision: str, cache_root: Path = DEFAULT_CACHE_ROOT,
                        repository_url: str = REPOSITORY_URL) -> Path:
    return _contract.fetch_pinned_source(_pin(cache_root=cache_root, repository_url=repository_url), revision)


def resolve_source(explicit: Path | None) -> tuple[Path, str]:
    """Return the source directory and how it was selected."""
    return _contract.resolve_source(_pin(), explicit)
