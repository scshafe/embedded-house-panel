#!/usr/bin/env python3
"""Reproducible Debian/QEMU simulation preparation and dry-run-first launch.

Writes only .build/sim and its private QMP socket. Never installs host software,
uses NXP firmware, edits another checkout, or treats simulation as FRDM evidence.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import platform
import shutil
import socket
import stat
import errno
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
SIM=ROOT/'.build/sim'
IMAGE_NAME='debian-13-generic-arm64-20260914-2601.qcow2'
IMAGE_URL='https://cloud.debian.org/images/cloud/trixie/20260914-2601/'+IMAGE_NAME
IMAGE_SHA512='bef60fa5c4f5511cf83fe1ade0279edd5318dccbbb44f14eb0aa7f3b467e038d7a7c0b6f837be0ad1c3b2e06c5ed8e27adfdf0fd111a201b8f4c9347cd414863'
EVIDENCE='simulation_not_hardware_evidence'


def emit(message: str, **fields: object) -> None:
    """Emit content-free structured progress and append the simulation transcript."""
    record={'timestamp':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'level':'info',
            'service':'bhp.virtual_panel','trace_id':'virtual-panel','message':message,
            'evidence':EVIDENCE,**fields}
    print(json.dumps(record),flush=True)
    if SIM.is_dir():
        with (SIM/'transcript.jsonl').open('a') as log:
            log.write(json.dumps(record)+'\n')


def qmp_path() -> Path:
    """Use a short per-checkout private path within Darwin's Unix socket limit."""
    return Path(tempfile.gettempdir())/('bhp-panel-'+hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]+'.qmp')


def qemu_program(name: str) -> str:
    bundle=ROOT/'.build/qemu/install/HousePanelQEMU.app/Contents/MacOS/qemu-system-aarch64'
    if name=='qemu-system-aarch64' and bundle.is_file():return str(bundle)
    local=ROOT/'.build/qemu/install/bin'/name
    return str(local) if local.is_file() else (shutil.which(name) or name)


def qemu_command(display: str='cocoa', port: int=2228) -> list[str]:
    """Construct the explicit architecture-compatible simulation command."""
    executable=qemu_program('qemu-system-aarch64')
    if 'HousePanelQEMU.app/Contents/MacOS/' in executable:
        prefix=ROOT/'.build/qemu/install'
    elif Path(executable).is_absolute():
        prefix=Path(executable).resolve().parents[1]
    else:
        prefix=Path('/opt/homebrew')
    firmware=prefix/'share/qemu/edk2-aarch64-code.fd'
    return [executable,'-name','House Panel - SIMULATION', '-machine','virt',
            '-accel','hvf','-cpu','host','-smp','4','-m','4096','-L',str(prefix/'share/qemu'),'-bios',str(firmware),
            '-drive','if=none,id=root,file='+str(SIM/'panel.qcow2')+',format=qcow2',
            '-device','virtio-blk-pci,drive=root',
            '-drive','if=none,id=seed,file='+str(SIM/'seed.iso')+',format=raw,readonly=on',
            '-device','virtio-blk-pci,drive=seed',
            '-device','virtio-gpu-pci,id=video0,xres=1024,yres=600',
            '-device','virtio-multitouch-pci','-device','virtio-tablet-pci',
            '-device','virtio-keyboard-pci','-display',display,
            '-netdev',f'user,id=net0,hostfwd=tcp:127.0.0.1:{port}-:22',
            '-device','virtio-net-pci,netdev=net0',
            '-qmp','unix:'+str(qmp_path())+',server=on,wait=off',
            '-serial','file:'+str(SIM/'serial.log'),'-monitor','none']


def prepare() -> None:
    """Prepare a hash-verified base, writable overlay and key-only cloud-init seed."""
    for program in (qemu_program('qemu-img'),'ssh-keygen','hdiutil'):
        if shutil.which(program) is None:
            raise ValueError('VIRTUAL_PANEL.DEPENDENCY_MISSING: '+program)
    if platform.system()!='Darwin' or platform.machine()!='arm64':
        raise ValueError('VIRTUAL_PANEL.LOCAL_ARM64_MAC_REQUIRED')
    os.umask(0o077); SIM.mkdir(parents=True,exist_ok=True)
    base=SIM/IMAGE_NAME
    if not base.exists():
        partial=base.with_suffix('.partial')
        emit('downloading_pinned_debian_image',source_url=IMAGE_URL)
        with urllib.request.urlopen(IMAGE_URL,timeout=60) as response, partial.open('wb') as output:
            shutil.copyfileobj(response,output,1024*1024)
        if hashlib.sha512(partial.read_bytes()).hexdigest()!=IMAGE_SHA512:
            partial.unlink()
            raise ValueError('VIRTUAL_PANEL.IMAGE_HASH_MISMATCH')
        partial.rename(base)
    elif hashlib.sha512(base.read_bytes()).hexdigest()!=IMAGE_SHA512:
        raise ValueError('VIRTUAL_PANEL.IMAGE_HASH_MISMATCH')
    disk=SIM/'panel.qcow2'
    if not disk.exists():
        subprocess.run([qemu_program('qemu-img'),'create','-f','qcow2','-F','qcow2','-b',str(base),str(disk),'24G'],check=True)
    key=SIM/'ssh_key'
    if not key.exists():
        subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-C','bhp-simulation-only','-f',str(key)],check=True)
    seed=SIM/'seed'; seed.mkdir(exist_ok=True)
    public=key.with_suffix('.pub').read_text().strip()
    config={'hostname':'bhp-sim','manage_etc_hosts':True,'ssh_pwauth':False,'disable_root':True,
            'users':[{'name':'paneladmin','shell':'/bin/bash','lock_passwd':True,
                      'sudo':'ALL=(ALL) NOPASSWD:ALL','ssh_authorized_keys':[public]}],
            'package_update':True,
            'packages':['build-essential','cmake','ninja-build','pkg-config','libdrm-dev','libpng-dev',
                        'libsdl2-dev','libssl-dev','libcurl4-openssl-dev','nlohmann-json3-dev',
                        'python3','python3-venv','linux-image-arm64','linux-headers-arm64','chrony','git','rsync','evtest','kmod'],
            'runcmd':[['systemctl','enable','--now','chrony'],['touch','/var/lib/bhp-sim-provisioned']]}
    (seed/'user-data').write_text('#cloud-config\n'+json.dumps(config,indent=2)+'\n')
    (seed/'meta-data').write_text('instance-id: bhp-panel-sim-v1\nlocal-hostname: bhp-sim\n')
    iso=SIM/'seed.iso'
    if not iso.exists():
        subprocess.run(['hdiutil','makehybrid','-iso','-joliet','-default-volume-name','cidata','-o',str(iso),str(seed)],check=True)
    (SIM/'inputs.json').write_text(json.dumps({'image_url':IMAGE_URL,'image_sha512':IMAGE_SHA512,
           'guest_architecture':'aarch64','ram_mib':4096,'disk_virtual_gib':24,'suspend':'disabled',
           'evidence':EVIDENCE},indent=2)+'\n')
    emit('guest_inputs_prepared',suspend='disabled',image_sha512=IMAGE_SHA512)


def select_cocoa_tablet(process: subprocess.Popen) -> None:
    """Select both absolute axes and buttons; query-mice current alone is insufficient."""
    from panel_ctl import QmpClient,select_tablet
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        if process.poll() is not None:
            raise ValueError('VIRTUAL_PANEL.QEMU_EXITED_BEFORE_INPUT_READY')
        try:
            with QmpClient(qmp_path()) as qmp:
                index=select_tablet(qmp)
            emit('cocoa_tablet_selected',tablet_index=index)
            return
        except (FileNotFoundError,ConnectionRefusedError):
            time.sleep(0.1)
    raise ValueError('VIRTUAL_PANEL.QMP_START_TIMEOUT')


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--display',choices=('cocoa','none'),default='cocoa')
    parser.add_argument('--ssh-port',type=int,default=2228)
    args=parser.parse_args()
    if not 1024<=args.ssh_port<=65535:
        raise ValueError('VIRTUAL_PANEL.PORT_INVALID')
    command=qemu_command(args.display,args.ssh_port)
    if not args.execute:
        print(json.dumps({'execute':False,'qemu_command':command,'preparation':{'image_url':IMAGE_URL,
              'sha512':IMAGE_SHA512},'suspend':'disabled','evidence':EVIDENCE}))
        return 0
    prepare()
    if args.prepare_only:
        return 0
    if qmp_path().exists():
        info=qmp_path().lstat()
        if not stat.S_ISSOCK(info.st_mode) or info.st_uid!=os.getuid():
            raise ValueError('VIRTUAL_PANEL.QMP_PATH_IDENTITY_INVALID')
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as probe:
            probe.settimeout(1)
            try:
                probe.connect(str(qmp_path()))
            except OSError as error:
                if error.errno!=errno.ECONNREFUSED:raise
                qmp_path().unlink()
                emit('removed_stale_owned_qmp_socket')
            else:
                raise ValueError('VIRTUAL_PANEL.ALREADY_RUNNING')
    command=qemu_command(args.display,args.ssh_port)
    if not Path(command[command.index('-bios')+1]).is_file():
        raise ValueError('VIRTUAL_PANEL.FIRMWARE_MISSING')
    (SIM/'launch.json').write_text(json.dumps({'command':command,'evidence':EVIDENCE},indent=2)+'\n')
    emit('launching_guest',display=args.display,ssh_port=args.ssh_port,suspend='disabled')
    process=subprocess.Popen(command,cwd=ROOT)
    try:
        select_cocoa_tablet(process)
    except (OSError,ValueError):
        process.terminate();process.wait(timeout=10)
        raise
    return process.wait()

if __name__=='__main__':
    try:
        raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print(json.dumps({'error_code':'VIRTUAL_PANEL.FAILED','detail':str(error),'evidence':EVIDENCE}),file=sys.stderr)
        raise SystemExit(1)
