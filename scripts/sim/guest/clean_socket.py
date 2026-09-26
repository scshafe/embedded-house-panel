#!/usr/bin/env python3
"""Remove only the stopped simulation service's exact stale socket inode."""
import os
import stat
import sys
from pathlib import Path
SOCKETS={'pir':('/run/bhp-sim/pir.sock',0),
         'touch':('/run/bhp-sim/touch/observations.sock',0),
         'ui':('/run/bhp-sim/ui/control.sock',910)}
if os.geteuid()!=0 or len(sys.argv)!=2 or sys.argv[1] not in SOCKETS:raise SystemExit(2)
name,uid=SOCKETS[sys.argv[1]];path=Path(name)
try:info=path.lstat()
except FileNotFoundError:raise SystemExit(0)
if not stat.S_ISSOCK(info.st_mode) or info.st_uid!=uid:raise SystemExit('SIM_GUEST.SOCKET_IDENTITY_MISMATCH')
path.unlink()
