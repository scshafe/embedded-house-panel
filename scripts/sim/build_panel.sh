#!/bin/sh
# Build headless/native/guest panel targets in an isolated CMake tree.
# Inputs: headless|native|guest [--execute]. Default is a JSON dry run.
# Side effects with --execute: pinned dependency download and .build products.
# Idempotent; never installs system software or changes production profiles.
set -eu
script_directory=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_directory/../.." && pwd)
mode=${1:-headless}
execute=${2:-}
case "$mode" in headless|native|guest) ;; *) exit 2 ;; esac
case "$execute" in ''|--execute) ;; *) exit 2 ;; esac
exec python3 - "$project_root" "$mode" "$execute" <<'PY'
import json, os, subprocess, sys
from pathlib import Path
root=Path(sys.argv[1]); mode=sys.argv[2]; execute=sys.argv[3]=='--execute'
build=root/'.build'/('panel-'+mode)
configure=['cmake','-S',str(root),'-B',str(build),'-G','Ninja',
           '-DCMAKE_BUILD_TYPE=Debug','-DBHP_BUILD_PANEL_RENDERER=ON',
           '-DBHP_BUILD_HTTP_LEXINET_CLIENT=ON',
           '-DBHP_BUILD_PANEL_SIMULATOR='+('ON' if mode=='native' else 'OFF'),
           '-DBHP_BUILD_PANEL_DRM='+('ON' if mode=='guest' else 'OFF')]
commands=[configure,['cmake','--build',str(build),'--parallel',str(min(6,os.cpu_count() or 2))]]
print(json.dumps({'mode':mode,'execute':execute,'commands':commands,'evidence':'simulation_not_hardware_evidence'}),flush=True)
if execute:
    for command in commands:
        subprocess.run(command,cwd=root,check=True)
PY
