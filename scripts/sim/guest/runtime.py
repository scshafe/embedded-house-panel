#!/usr/bin/env python3
"""Guest-only device discovery and exact simulation service configuration.

Root discovers devices once; the UI reads only this configuration, never evdev.
All paths and identities belong to ADR 0009's disposable Debian guest.
"""
from __future__ import annotations
import datetime
import fcntl
import json
import os
import socket
import struct
import subprocess
from pathlib import Path

ROOT=Path('/opt/bhp-sim')
STATE=Path('/var/lib/bhp-sim')
RUN=Path('/run/bhp-sim')
EVIDENCE='simulation_not_hardware_evidence'


def invoke(*command):
    return subprocess.run(command,check=True,capture_output=True,text=True).stdout.strip()


def setup_directories():
    RUN.mkdir(exist_ok=True,mode=0o755)
    for name in ('lifecycle','touch'):
        path=RUN/name;path.mkdir(exist_ok=True);os.chown(path,0,910);path.chmod(0o2750)
    path=RUN/'ui';path.mkdir(exist_ok=True);os.chown(path,910,910);path.chmod(0o700)
    STATE.mkdir(exist_ok=True,mode=0o755)
    path=STATE/'study';path.mkdir(exist_ok=True);os.chown(path,910,910);path.chmod(0o700)


def touch_device(protocol):
    selected=[]
    for entry in Path('/sys/class/input').glob('event*'):
        name=(entry/'device/name').read_text().strip()
        expected='QEMU Virtio MultiTouch' if protocol=='type_b' else 'QEMU Virtio Tablet'
        if name!=expected:continue
        device=Path('/dev/input')/entry.name
        with device.open('rb',buffering=0) as fd:
            def axis(number):
                return struct.unpack('iiiiii',fcntl.ioctl(fd, (2<<30)|(24<<16)|(ord('E')<<8)|(0x40+number),bytes(24)))
            x=axis(0x35 if protocol=='type_b' else 0); y=axis(0x36 if protocol=='type_b' else 1)
            slots=axis(0x2f)[2]+1 if protocol=='type_b' else 1
        selected.append({'path':str(device),'name':name,'protocol':protocol,'x_min':x[1],'x_max':x[2],
                         'y_min':y[1],'y_max':y[2],'slots':slots,'contact_key':0 if protocol=='type_b' else 272})
    if len(selected)!=1:raise ValueError('SIM_GUEST.EXACT_TOUCH_DEVICE_REQUIRED')
    return selected[0]


def drm_device():
    selected=[]
    for connector in Path('/sys/class/drm').glob('card*-*'):
        if not (connector/'status').exists() or (connector/'status').read_text().strip()!='connected':continue
        if '1024x600' not in (connector/'modes').read_text().splitlines():continue
        selected.append({'path':'/dev/dri/'+connector.name.split('-')[0],
                         'connector_id':int((connector/'connector_id').read_text())})
    if len(selected)!=1:raise ValueError('SIM_GUEST.EXACT_DRM_CONNECTOR_REQUIRED')
    return selected[0]


def configure(protocol='single',solar='day'):
    setup_directories()
    now=datetime.datetime.now(datetime.timezone.utc)
    utc_hour=now.hour+now.minute/60+now.second/3600
    longitude=((12 if solar=='day' else 0)-utc_hour)*15
    longitude=(longitude+180)%360-180
    touch=touch_device(protocol);drm=drm_device()
    values={'LATITUDE':0,'LONGITUDE':longitude,'PRESENCE_GRACE_SECONDS':3,
        'PRESENCE_INPUT_NAME':'bhp-sim-pir','BACKLIGHT_PATH':'/sys/class/backlight/bhp-sim/brightness',
        'BACKLIGHT_MAX_PATH':'/sys/class/backlight/bhp-sim/max_brightness',
        'MODE_SOCKET':str(RUN/'lifecycle/mode.sock'),'PANEL_UI_UID':910,
        'PRESENTATION_TIMEOUT_MILLISECONDS':1500,'DAY_BRIGHTNESS':70,'NIGHT_BRIGHTNESS':12,
        'GRACE_DAY_BRIGHTNESS':60,'GRACE_NIGHT_BRIGHTNESS':4,'OFF_BRIGHTNESS':0,
        'SUSPEND_MODE':'disabled','POLL_INTERVAL_MILLISECONDS':1000,
        'TOUCH_INPUT_PATH':touch['path'],'TOUCH_INPUT_NAME':touch['name'],
        'TOUCH_PROTOCOL':protocol,'TOUCH_X_MIN':touch['x_min'],'TOUCH_X_MAX':touch['x_max'],
        'TOUCH_Y_MIN':touch['y_min'],'TOUCH_Y_MAX':touch['y_max'],'TOUCH_CONTACT_KEY':touch['contact_key'],
        'TOUCH_SLOTS':touch['slots'],'TOUCH_SOCKET':str(RUN/'touch/observations.sock'),
        'TOUCH_TTL_MILLISECONDS':1500,'TOUCH_MAX_CONTACT_MILLISECONDS':4000,'TOUCH_MAX_OBSERVATIONS':128}
    (STATE/'supervisor.env').write_text(''.join('BHP_PANEL_SUPERVISOR_'+key+'='+str(value)+'\n' for key,value in values.items()))
    config={'touch':touch,'drm':drm,'solar':solar,'longitude':longitude,'evidence':EVIDENCE}
    (STATE/'runtime.json').write_text(json.dumps(config,indent=2)+'\n')
    return config


def ui_command():
    config=json.loads((STATE/'runtime.json').read_text());touch=config['touch'];drm=config['drm']
    command=[str(ROOT/'.build/panel-guest/panel_ui/bhp-panel-ui'),'--simulation-drm',
        '--socket-path',str(RUN/'lifecycle/mode.sock'),'--supervisor-uid','0',
        '--drm-device',drm['path'],'--drm-connector-id',str(drm['connector_id']),
        '--touch-socket-path',str(RUN/'touch/observations.sock'),
        '--simulation-control-socket',str(RUN/'ui/control.sock'),
        '--scenario',str(ROOT/'simulator/scenarios/day-front.json'),
        '--state-directory',str(STATE/'study')]
    for name in ('min-x','max-x','min-y','max-y'):
        first,last=name.split('-');command+=['--touch-'+name,str(touch[last+'_'+first])]
    if (STATE/'lexinet.json').exists():command+=['--lexinet-config',str(STATE/'lexinet.json')]
    return command


if __name__=='__main__':
    import sys
    if sys.argv[1:] == ['ui']:
        command=ui_command();os.execv(command[0],command)
    elif sys.argv[1:] == ['configure']:
        print(json.dumps(configure()))
    else:raise SystemExit(2)
