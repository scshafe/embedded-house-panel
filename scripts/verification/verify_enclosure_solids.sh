#!/bin/sh
# Builds the mirror unit's printed parts and runs the solid check, when the
# CAD environment exists (make enclosure-venv creates it; CI does).
# Inputs: optional BHP_ENCLOSURE_PYTHON. Side effects: none (check only).

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_enclosure_solids
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

python=${BHP_ENCLOSURE_PYTHON:-$PROJECT_ROOT/.build/enclosure/venv/bin/python}
if [ ! -x "$python" ]; then
  bhp_log info enclosure_solid_check_skipped "$TRACE_ID" reason cad_environment_missing \
    remedy "make enclosure-venv"
  exit 0
fi
PYTHONDONTWRITEBYTECODE=1 "$python" "$PROJECT_ROOT/enclosure/cad/build_enclosure.py" --no-export --check-doc
(cd "$PROJECT_ROOT" && PYTHONDONTWRITEBYTECODE=1 "$python" -m unittest discover -s enclosure/cad/tests -p 'test_*.py')
bhp_log info enclosure_solid_check_complete "$TRACE_ID"
