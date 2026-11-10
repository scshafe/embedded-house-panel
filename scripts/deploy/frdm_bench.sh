#!/bin/sh
# The bench tier on the FRDM's vendor image (docs/decisions/0014-run-a-bench-tier-on-the-vendor-image.md).
# Inputs: build | install | rollback | uninstall [--purge] | status | logs SERVICE, then optional --execute.
# Without --execute it prints a JSON plan and touches nothing; see frdm_bench/bench_pc.py for each command.
# Side effects with --execute: build writes .build/frdm-bench* only; board commands need
#   BHP_BOARD_SSH_TARGET and write the board only as ADR 0014 allows (/opt/house-panel, the
#   bench units, weston's links, /run). Records go to hardware_tests/results/frdm/bench-<UTC>/.
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
exec python3 -I "$script_directory/frdm_bench/bench_pc.py" "$@"
