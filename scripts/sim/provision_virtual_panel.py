#!/usr/bin/env python3
"""Upload this checkout and provision the already booted disposable Debian guest.

Dry-run default; --execute builds source inside Linux and creates guest-only
services. The ignored CA certificate is copied, never its private key.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import shlex
import subprocess
import tarfile
import time
from fixture_https import ensure_server,TLS
from virtual_panel_vm import ROOT,SIM,EVIDENCE,emit


def ssh_arguments(port):
    return ['-o','BatchMode=yes','-o','ConnectTimeout=10','-o','StrictHostKeyChecking=accept-new',
            '-o','UserKnownHostsFile='+str(SIM/'known_hosts'),'-i',str(SIM/'ssh_key'),'-p',str(port)]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execute',action='store_true')
    parser.add_argument('--ssh-port',type=int,default=2228);args=parser.parse_args()
    if not 1024<=args.ssh_port<=65535:raise ValueError('SIM_PROVISION.PORT_INVALID')
    if not args.execute:
        print(json.dumps({'execute':False,'source':str(ROOT),'guest_destination':'/opt/bhp-sim',
                          'guest_only_services':True,'evidence':EVIDENCE}));return 0
    ensure_server()
    release=subprocess.check_output([str(ROOT/'scripts/build/application_release_id.sh'),str(ROOT)],text=True).strip()
    paths=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=ROOT).decode().split('\0')
    archive=SIM/'source.tar.gz'
    with tarfile.open(archive,'w:gz') as output:
        for name in sorted(set(paths)):
            if not name or name.startswith('.claude/') or not (ROOT/name).exists():continue
            output.add(ROOT/name,arcname=name,recursive=False)
    emit('guest_source_archived',release_id=release,sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
    ssh=['ssh',*ssh_arguments(args.ssh_port),'paneladmin@127.0.0.1']
    deadline=time.monotonic()+180
    while time.monotonic()<deadline:
        probe=subprocess.run(ssh+['true'],capture_output=True)
        if probe.returncode==0:break
        time.sleep(2)
    else:raise ValueError('SIM_PROVISION.SSH_NOT_READY')
    subprocess.run(ssh+['cloud-init status --wait >/dev/null'],check=True)
    ready=subprocess.run(ssh+['test -d /lib/modules/$(uname -r)/build'],capture_output=True)
    if ready.returncode:
        emit('guest_reboot_for_matching_kernel_headers')
        subprocess.run(ssh+['sudo reboot'],capture_output=True)
        deadline=time.monotonic()+180
        time.sleep(3)
        while time.monotonic()<deadline:
            probe=subprocess.run(ssh+['test -d /lib/modules/$(uname -r)/build'],capture_output=True)
            if probe.returncode==0:break
            time.sleep(2)
        else:raise ValueError('SIM_PROVISION.KERNEL_HEADERS_NOT_READY')
    # Both paths are fixed task-owned locations. No production files are touched.
    subprocess.run(ssh+['sudo install -d -m 755 /opt/bhp-sim /var/lib/bhp-sim'],check=True)
    for source,target in ((archive,'/tmp/bhp-sim-source.tar.gz'),(TLS/'certificate.pem','/tmp/bhp-sim-ca.pem'),(SIM/'guest-lexinet.json','/tmp/bhp-sim-lexinet.json')):
        with source.open('rb') as stream:subprocess.run(ssh+['cat > '+shlex.quote(target)],stdin=stream,check=True)
    subprocess.run(ssh+['sudo tar -xzf /tmp/bhp-sim-source.tar.gz -C /opt/bhp-sim && sudo install -m 644 /tmp/bhp-sim-ca.pem /var/lib/bhp-sim/fixture-ca.pem && sudo install -m 644 /tmp/bhp-sim-lexinet.json /var/lib/bhp-sim/lexinet.json'],check=True)
    command=['sudo','python3','/opt/bhp-sim/scripts/sim/guest/provision.py','--release-id',release]
    subprocess.run(ssh+[shlex.join(command)],check=True)
    emit('guest_provision_complete',release_id=release)
    return 0


if __name__=='__main__':raise SystemExit(main())
