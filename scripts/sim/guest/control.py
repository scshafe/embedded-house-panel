#!/usr/bin/env python3
"""Root-only bounded JSON fixture control, never a production capability endpoint."""
import json
import os
import re
import socket
import subprocess
import sys
import time
from runtime import ROOT,RUN,STATE,EVIDENCE,configure,invoke


def datagram(path,payload):
    with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as channel:
        channel.sendto(payload,str(path))


def status():
    services={name:subprocess.run(['systemctl','is-active','bhp-sim-'+name],capture_output=True,text=True).stdout.strip()
              for name in ('pir','supervisor','ui')}
    records=invoke('journalctl','-u','bhp-sim-ui','-b','-n','200','--no-pager','-o','json').splitlines()
    latest=None;last_touch=None
    for record in records:
        try:
            journal=json.loads(record);entry=json.loads(journal['MESSAGE']);data=entry['data']
            scene=data.get('status',data)
            if 'scenario_id' in scene:latest=scene
            if data.get('touch_commit') is True:
                last_touch={'cursor':journal['__CURSOR'],'status':scene}
        except (KeyError,ValueError,TypeError):continue
    return {'services':services,'brightness':int(open('/sys/class/backlight/bhp-sim/brightness').read()),
            'runtime':json.loads((STATE/'runtime.json').read_text()),'ui':latest,'last_touch':last_touch,
            'suspend':'disabled','evidence':EVIDENCE}


def apply(message):
    kind=message.get('type')
    if kind=='status' and set(message)=={'type'}:return status()
    if kind=='presence' and set(message)=={'type','present'} and type(message['present']) is bool:
        datagram(RUN/'pir.sock',b'1' if message['present'] else b'0');time.sleep(1.2);return status()
    if kind=='select_touch' and set(message)=={'type','protocol'} and message['protocol'] in ('single','type_b'):
        current=json.loads((STATE/'runtime.json').read_text())
        invoke('systemctl','stop','bhp-sim-ui','bhp-sim-supervisor')
        configure(message['protocol'],current['solar'])
        invoke('systemctl','start','bhp-sim-supervisor','bhp-sim-ui');time.sleep(2);return status()
    if (kind=='clock_preview' and set(message)=={'type','enabled'} and type(message['enabled']) is bool) or (kind=='clock_step' and set(message)=={'type'}):
        datagram(RUN/'ui/control.sock',json.dumps(message).encode());time.sleep(0.6);return status()
    if kind=='scenario' and set(message)=={'type','scenario_id'}:
        name=message['scenario_id']
        if not isinstance(name,str) or not re.fullmatch('[a-z0-9_-]{1,80}',name):raise ValueError('SIM_GUEST.SCENARIO_INVALID')
        path=ROOT/'simulator/scenarios'/(name+'.json')
        scenario=json.loads(path.read_text())
        solar='night' if scenario.get('lifecycle')=='night' else 'day'
        current=json.loads((STATE/'runtime.json').read_text())
        if current['solar']!=solar:
            invoke('systemctl','stop','bhp-sim-ui','bhp-sim-supervisor');configure(current['touch']['protocol'],solar)
            invoke('systemctl','start','bhp-sim-supervisor','bhp-sim-ui');time.sleep(2)
        datagram(RUN/'pir.sock',b'1');time.sleep(1.2)
        datagram(RUN/'ui/control.sock',json.dumps({'type':'scenario','path':str(path)}).encode());time.sleep(1.2)
        if name in ('blank','grace'):
            datagram(RUN/'pir.sock',b'0');time.sleep(4.2 if name=='blank' else 1.1)
        return status()
    raise ValueError('SIM_GUEST.CONTROL_INVALID')


if __name__=='__main__':
    try:
        if os.geteuid()!=0:raise ValueError('SIM_GUEST.ROOT_REQUIRED')
        payload=sys.stdin.buffer.readline(4097)
        if len(payload)>4096:raise ValueError('SIM_GUEST.FRAME_TOO_LARGE')
        print(json.dumps({'data':apply(json.loads(payload)),'error':None,'evidence':EVIDENCE}))
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print(json.dumps({'data':None,'error':{'code':'SIM_GUEST.CONTROL_FAILED','detail':str(error)},'evidence':EVIDENCE}))
        raise SystemExit(1)
