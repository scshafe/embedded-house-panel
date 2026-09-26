#!/usr/bin/env python3
"""Dry-run-first native SDL launch using the persistent verified-TLS fixture."""
import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
# Local support imports must not create cache files during the JSON dry run.
sys.dont_write_bytecode=True
from fixture_https import ensure_server
from virtual_panel_vm import ROOT,SIM,EVIDENCE


def prepare_bundle(source, bundle):
    """Create only a checkout-local app identity; retain the raw test executable."""
    if sys.platform != 'darwin':raise ValueError('SIM_NATIVE.MACOS_REQUIRED')
    executable=bundle/'Contents/MacOS/bhp-panel-sim'
    executable.parent.mkdir(parents=True,exist_ok=True)
    # Atomic replacement preserves the inode of a previously running instance.
    descriptor,temporary=tempfile.mkstemp(prefix='.bhp-panel-sim-',dir=executable.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source,temporary)
        os.replace(temporary,executable)
    finally:
        if os.path.exists(temporary):os.unlink(temporary)
    with (bundle/'Contents/Info.plist').open('wb') as output:
        plistlib.dump({'CFBundleIdentifier':'org.housepanel.native-simulation',
                      'CFBundleName':'House Panel Simulator',
                      'CFBundleDisplayName':'House Panel Simulator',
                      'CFBundleExecutable':'bhp-panel-sim','CFBundlePackageType':'APPL',
                      'CFBundleInfoDictionaryVersion':'6.0','CFBundleVersion':'0.1.0',
                      'CFBundleShortVersionString':'0.1.0',
                      'NSHighResolutionCapable':True,'LSUIElement':False},output)
    subprocess.run(['codesign','--force','--sign','-',str(bundle)],check=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execute',action='store_true')
    parser.add_argument('--scenario',type=Path,default=ROOT/'simulator/scenarios/home.json')
    args=parser.parse_args()
    source=ROOT/'.build/panel-native/simulator/bhp-panel-sim'
    bundle=SIM/'HousePanelSimulator.app'
    command=[str(bundle/'Contents/MacOS/bhp-panel-sim'),'--scenario',str(args.scenario.resolve()),
             '--clock-preview','on','--state-directory',str(SIM/'native-study'),'--lexinet-config',str(SIM/'native-lexinet.json')]
    print(json.dumps({'execute':args.execute,'command':command,'fixture_https':True,
                     'app_bundle':{'path':str(bundle),'source_executable':str(source),
                                   'bundle_id':'org.housepanel.native-simulation',
                                   'scope':'checkout-local only'},'evidence':EVIDENCE}),flush=True)
    if not args.execute:return 0
    prepare_bundle(source,bundle)
    ensure_server()
    state=SIM/'native-study';state.mkdir(exist_ok=True,mode=0o700)
    return subprocess.call(command,cwd=ROOT)


if __name__=='__main__':raise SystemExit(main())
