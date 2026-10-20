#!/usr/bin/env python3
"""Check the mirror unit's layout and keep its drawings and tables current.

    python3 enclosure/fit_check.py            # run the fit check
    python3 enclosure/fit_check.py --check    # also fail if a generated file is stale
    python3 enclosure/fit_check.py --write    # regenerate the drawings and tables

Reads ``enclosure/params.toml``. The drawings go to
``docs/hardware/mirror-unit/*.svg``; the harness and caliper tables replace
the generated blocks in ``docs/hardware/mirror-unit-layout.md``. Standard
library only, so ``make verify`` runs it on any host.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "enclosure"))

from bhp_enclosure import checks, drawings, model  # noqa: E402

PARAMS = ROOT / "enclosure/params.toml"
DRAWINGS = ROOT / "docs/hardware/mirror-unit"
LAYOUT_DOC = ROOT / "docs/hardware/mirror-unit-layout.md"
BLOCK = re.compile(r"(<!-- generated:(?P<name>[a-z-]+) -->\n)(?P<body>.*?)(<!-- /generated:(?P=name) -->)",
                   re.DOTALL)


def generated_blocks(layout: model.Layout) -> dict[str, str]:
    return {"harness": drawings.harness_table(layout), "calipers": drawings.caliper_table(layout)}


def render_document(text: str, blocks: dict[str, str], path: Path = LAYOUT_DOC) -> str:
    seen = set()

    def replace(match: re.Match[str]) -> str:
        name = match.group("name")
        if name not in blocks:
            raise SystemExit(f"ENCLOSURE.UNKNOWN_BLOCK {name} in {path.relative_to(ROOT)}")
        seen.add(name)
        return match.group(1) + blocks[name] + match.group(4)

    rendered = BLOCK.sub(replace, text)
    missing = set(blocks) - seen
    if missing:
        raise SystemExit(f"ENCLOSURE.MISSING_BLOCK {', '.join(sorted(missing))} in {path.relative_to(ROOT)}")
    return rendered


def expected_files(layout: model.Layout) -> dict[Path, str]:
    files = {DRAWINGS / name: svg for name, svg in drawings.drawings(layout).items()}
    files[LAYOUT_DOC] = render_document(LAYOUT_DOC.read_text(encoding="utf-8"), generated_blocks(layout))
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="fail if a generated file is stale")
    mode.add_argument("--write", action="store_true", help="regenerate the drawings and tables")
    parser.add_argument("--params", type=Path, default=PARAMS)
    parser.add_argument("--json", action="store_true", help="print findings as JSON lines")
    arguments = parser.parse_args(argv)
    try:
        layout = model.load(arguments.params)
    except model.ModelError as error:
        print(f"ENCLOSURE.{error.code} {error.detail}", file=sys.stderr)
        return 1
    findings = checks.run(layout)
    for finding in findings:
        if arguments.json:
            print(json.dumps({"code": finding.code, "subject": finding.subject, "detail": finding.detail},
                             sort_keys=True))
        else:
            print(finding, file=sys.stderr)
    status = 1 if findings else 0
    if arguments.write or arguments.check:
        stale = []
        for path, content in expected_files(layout).items():
            current = path.read_text(encoding="utf-8") if path.exists() else None
            if current == content:
                continue
            if arguments.write:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            else:
                stale.append(path.relative_to(ROOT).as_posix())
        known = {path.name for path in expected_files(layout) if path.parent == DRAWINGS}
        extra = sorted(path.name for path in DRAWINGS.glob("*.svg") if path.name not in known) \
            if DRAWINGS.exists() else []
        if extra and arguments.write:
            for name in extra:
                (DRAWINGS / name).unlink()
        elif extra:
            stale += [f"docs/hardware/mirror-unit/{name} (not generated)" for name in extra]
        if stale:
            print("ENCLOSURE.GENERATED_STALE " + ", ".join(stale) +
                  "; run python3 enclosure/fit_check.py --write", file=sys.stderr)
            status = 1
    statuses: dict[str, int] = {}
    for part in layout.parts:
        for item in (*part.shapes, *part.holes, *part.connectors, *part.points):
            statuses[item.provenance.status] = statuses.get(item.provenance.status, 0) + 1
    summary = " ".join(f"{name}={statuses.get(name, 0)}" for name in model.STATUSES)
    print(f"ENCLOSURE.FIT_CHECK findings={len(findings)} parts={len(layout.parts)} "
          f"cables={len(layout.cables)} {summary}")
    return status


if __name__ == "__main__":
    sys.exit(main())
