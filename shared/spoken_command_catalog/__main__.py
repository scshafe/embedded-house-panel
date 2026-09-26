"""Analyze catalog and build reproducible fake artifacts; writes only with --output."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from .catalog import CatalogError, build_bundle, canonical, digest, load_json, validate_catalog

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        catalog = load_json(args.root / "docs/interfaces/spoken-command-catalog.json")
        reports = validate_catalog(catalog)
        artifacts = {"confusability-report": {"metric_version": "nfc-casefold-punctuation-space-arpabet-unit-levenshtein-v1", "catalog_revision": catalog["catalog_revision"], "pairs": reports, "evidence": "semantic_fixture_not_acoustic_qualification"}}
        for name, contexts in {"front": ["study_front"], "front-history": ["study_front", "study_history"], "back": ["study_back"], "back-history": ["study_back", "study_history"]}.items():
            artifacts[name] = build_bundle(args.root, contexts)
        if args.output:
            args.output.mkdir(parents=True, exist_ok=True)
            for name, artifact in artifacts.items():
                (args.output / (name + ".json")).write_bytes(canonical(artifact) + b"\n")
        print(json.dumps({"ok": True, "artifact_digests": {name: digest(value) for name, value in artifacts.items()}, "evidence": "semantic_fixture_not_acoustic_qualification"}, sort_keys=True))
        return 0
    except CatalogError as error:
        print(json.dumps({"ok": False, "error": {"code": error.code, "operation": error.operation}}))
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
