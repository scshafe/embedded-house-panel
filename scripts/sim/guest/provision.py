#!/usr/bin/env python3
"""Provision the disposable guest and replace its running simulation services."""
import argparse
import json
import os
import platform
import subprocess
from pathlib import Path
from runtime import ROOT,EVIDENCE,invoke

RUNTIME_SERVICES=('ui','supervisor','configure','pir','devices')


def unit(name,text):
    Path('/etc/systemd/system/bhp-sim-'+name+'.service').write_text(text)


def stop_runtime():
    # Stop consumers before replacing their executables or rediscovering devices.
    # Missing units are expected only on the first provisioning run.
    for name in RUNTIME_SERVICES:
        service='bhp-sim-'+name+'.service'
        if Path('/etc/systemd/system',service).exists():
            invoke('systemctl','stop',service)
    # Upgrade the original devices unit, which had no ExecStop. New units unload
    # themselves; this also recovers a module left by an interrupted first run.
    if Path('/sys/module/bhp_sim_backlight').exists():
        invoke('/usr/sbin/rmmod','bhp_sim_backlight')


def start_runtime():
    invoke('systemctl','daemon-reload')
    invoke('systemctl','enable','--now','bhp-sim-pir','bhp-sim-supervisor','bhp-sim-ui')
    for name in RUNTIME_SERVICES:
        invoke('systemctl','is-active','--quiet','bhp-sim-'+name+'.service')
    pids={name:int(invoke('systemctl','show','--property=MainPID','--value',
                         'bhp-sim-'+name+'.service'))
          for name in ('ui','supervisor','pir')}
    if any(pid<=0 for pid in pids.values()):raise ValueError('SIM_GUEST.RUNTIME_NOT_RUNNING')
    return pids


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--release-id',required=True);args=parser.parse_args()
    if os.geteuid()!=0 or platform.machine()!='aarch64' or Path('/etc/hostname').read_text().strip()!='bhp-sim':
        raise ValueError('SIM_GUEST.IDENTITY_REQUIRED')
    if not Path('/var/lib/bhp-sim-provisioned').exists():raise ValueError('SIM_GUEST.CLOUD_INIT_INCOMPLETE')
    if not Path('/lib/modules/'+platform.release()+'/build').exists():
        raise ValueError('SIM_GUEST.REBOOT_REQUIRED_FOR_INSTALLED_KERNEL_HEADERS')
    for name,uid in (('bhp-panel',910),('bhp-media',911),('bhp-context',912)):
        if subprocess.run(['id',name],capture_output=True).returncode:
            invoke('useradd','--system','--uid',str(uid),'--user-group','--no-create-home','--shell','/usr/sbin/nologin',name)
        elif int(invoke('id','-u',name))!=uid:raise ValueError('SIM_GUEST.UID_COLLISION')
    invoke('usermod','-a','-G','video','bhp-panel')
    subprocess.run(['cmake','-S',str(ROOT),'-B',str(ROOT/'.build/panel-guest'),'-G','Ninja',
                    '-DCMAKE_BUILD_TYPE=Debug','-DBHP_BUILD_PANEL_DRM=ON','-DBHP_BUILD_HTTP_LEXINET_CLIENT=ON',
                    '-DBHP_APPLIANCE_RELEASE_ID='+args.release_id],check=True)
    subprocess.run(['cmake','--build',str(ROOT/'.build/panel-guest'),'--parallel','4'],check=True)
    subprocess.run(['ctest','--test-dir',str(ROOT/'.build/panel-guest'),'--output-on-failure'],check=True)
    module=ROOT/'scripts/sim/guest/backlight'
    subprocess.run(['make','-C','/lib/modules/'+platform.release()+'/build','M='+str(module),'modules'],check=True)
    pir_binary=ROOT/'.build/panel-guest/bhp-sim-pir'
    invoke('cc','-std=gnu11','-Wall','-Wextra','-Werror','-O2',str(ROOT/'scripts/sim/guest/pir_uinput.c'),'-o',str(pir_binary))
    invoke('chronyc','waitsync','30','0.1')
    stop_runtime()
    invoke('install','-m','755','-d','/usr/local/lib/bhp-sim')
    invoke('install','-m','644',str(module/'bhp_sim_backlight.ko'),'/usr/local/lib/bhp-sim/backlight.ko')
    invoke('install','-m','755',str(pir_binary),'/usr/local/lib/bhp-sim/pir')
    unit('devices','''[Unit]
Description=Guest-only virtual panel devices (simulation)
After=systemd-udevd.service
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/sbin/modprobe uinput
ExecStart=/usr/sbin/insmod /usr/local/lib/bhp-sim/backlight.ko
ExecStart=/usr/bin/install -d -m 755 /run/bhp-sim
ExecStop=/usr/sbin/rmmod bhp_sim_backlight
[Install]
WantedBy=multi-user.target
''')
    unit('pir','''[Unit]
Description=Guest-only persistent PIR simulation
Requires=bhp-sim-devices.service
After=bhp-sim-devices.service
[Service]
ExecStartPre=/usr/bin/python3 /opt/bhp-sim/scripts/sim/guest/clean_socket.py pir
ExecStart=/usr/local/lib/bhp-sim/pir
Restart=on-failure
[Install]
WantedBy=multi-user.target
''')
    unit('configure','''[Unit]
Description=Discover exact guest display and input identities
Requires=bhp-sim-pir.service
After=bhp-sim-pir.service systemd-udev-settle.service
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/bin/python3 /opt/bhp-sim/scripts/sim/guest/runtime.py configure
''')
    unit('supervisor','''[Unit]
Description=Real panel supervisor on simulated hardware
Requires=bhp-sim-configure.service
After=bhp-sim-configure.service chrony.service
[Service]
EnvironmentFile=/var/lib/bhp-sim/supervisor.env
ExecStartPre=/usr/bin/python3 /opt/bhp-sim/scripts/sim/guest/clean_socket.py touch
ExecStart=/opt/bhp-sim/.build/panel-guest/panel_supervisor/panel_supervisord
Restart=on-failure
RestartSec=2
[Install]
WantedBy=multi-user.target
''')
    unit('ui','''[Unit]
Description=Real lifecycle and DRM panel UI (simulation only)
Requires=bhp-sim-supervisor.service
After=bhp-sim-supervisor.service
[Service]
User=bhp-panel
Group=bhp-panel
SupplementaryGroups=video
WorkingDirectory=/opt/bhp-sim
ExecStartPre=+/usr/bin/python3 /opt/bhp-sim/scripts/sim/guest/clean_socket.py ui
ExecStart=/usr/bin/python3 /opt/bhp-sim/scripts/sim/guest/runtime.py ui
Restart=on-failure
RestartSec=2
NoNewPrivileges=yes
[Install]
WantedBy=multi-user.target
''')
    service_pids=start_runtime()
    print(json.dumps({'provisioned':True,'release_id':args.release_id,
                      'service_pids':service_pids,'suspend':'disabled','evidence':EVIDENCE}))


if __name__=='__main__':main()
