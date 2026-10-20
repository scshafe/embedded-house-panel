#!/usr/bin/env python3
"""Resolve the voice-lab source for host-side contract checks.

The voice-lab configuration of ``scripts/contracts/contract_pin.py``: the pin
is the ``repository_base_revision`` in the checked-in corpus-client.v1
provenance, the cache is ``.build/voice-lab/source/``, and error codes are
``VOICE_LAB_*``. An explicit ``--voice-lab-source-dir`` checkout is used as
given.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/contracts"))

import contract_pin as _contract  # noqa: E402

from contract_pin import ContractPin, PinnedSourceError  # noqa: E402,F401

REPOSITORY_URL = "https://github.com/scshafe/voice-lab"
PROVENANCE = ROOT / "voice_lab_recorder/tests/fixtures/corpus_client_v1/contract-provenance.json"
DEFAULT_CACHE_ROOT = ROOT / ".build/voice-lab/source"
CONTRACT_ROOT = "contracts/corpus-client.v1"


def pin(provenance: Path = PROVENANCE, cache_root: Path = DEFAULT_CACHE_ROOT,
        repository_url: str = REPOSITORY_URL) -> ContractPin:
    return ContractPin("VOICE_LAB", repository_url, provenance, cache_root)


def pinned_revision(provenance: Path = PROVENANCE) -> str:
    return _contract.pinned_revision(pin(provenance))


def resolve_source(explicit: Path | None) -> tuple[Path, str]:
    """Return the source directory and how it was selected."""
    return _contract.resolve_source(pin(), explicit)
