#!/usr/bin/env python3
"""Dry-run-first QMP/guest control for the isolated virtual panel.

--execute permits taps, swipe, presence, scenario selection and PNG capture.
QMP carries emulated input and screenshots. SSH invokes one bounded guest control
endpoint for uinput PIR and fixture selection; no feature uses the lifecycle socket.
"""
from __future__ import annotations
import argparse
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path
from virtual_panel_vm import ROOT,SIM,EVIDENCE,emit,qmp_path

class QmpClient:
    """Bounded synchronous QMP client, matching response IDs while retaining events."""
    def __init__(self,path:Path):
        self.connection=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        self.connection.settimeout(10)
        try:self.connection.connect(str(path))
        except OSError:
            self.connection.close();raise
        self.stream=self.connection.makefile('rwb'); self.sequence=0
        if 'QMP' not in self._read():
            self.close(); raise ValueError('PANEL_CTL.QMP_GREETING_INVALID')
        self.command('qmp_capabilities')
    def _read(self) -> dict:
        line=self.stream.readline(65537)
        if not line or len(line)>65536:
            raise ValueError('PANEL_CTL.QMP_FRAME_INVALID')
        message=json.loads(line)
        if not isinstance(message,dict): raise ValueError('PANEL_CTL.QMP_FRAME_INVALID')
        return message
    def command(self,execute:str,arguments:dict|None=None) -> object:
        self.sequence+=1
        request={'execute':execute,'id':self.sequence}
        if arguments is not None: request['arguments']=arguments
        self.stream.write((json.dumps(request)+'\n').encode()); self.stream.flush()
        for _ in range(256):
            reply=self._read()
            if 'event' in reply: continue
            if reply.get('id')!=self.sequence: raise ValueError('PANEL_CTL.QMP_ID_MISMATCH')
            if 'error' in reply: raise ValueError('PANEL_CTL.QMP_ERROR: '+json.dumps(reply['error']))
            if 'return' not in reply: raise ValueError('PANEL_CTL.QMP_REPLY_INVALID')
            return reply['return']
        raise ValueError('PANEL_CTL.QMP_EVENT_LIMIT')
    def close(self):
        self.stream.close(); self.connection.close()
    def __enter__(self): return self
    def __exit__(self,*args): self.close()


def select_tablet(qmp:QmpClient) -> int:
    """Prioritize the one exact tablet's ABS and BTN handlers, including Cocoa input."""
    tablets=[item for item in qmp.command('query-mice')
             if item['name']=='QEMU Virtio Tablet' and item['absolute']]
    if len(tablets)!=1:raise ValueError('PANEL_CTL.EXACT_TABLET_REQUIRED')
    index=tablets[0]['index']
    if type(index) is not int or index<0:raise ValueError('PANEL_CTL.TABLET_INDEX_INVALID')
    reply=qmp.command('human-monitor-command',{'command-line':'mouse_set '+str(index)})
    if reply:raise ValueError('PANEL_CTL.TABLET_SELECTION_FAILED')
    return index


def coordinates(x:int,y:int) -> tuple[int,int]:
    """Map explicit 1024x600 coordinates to QEMU's inclusive absolute range."""
    if type(x) is not int or type(y) is not int or not 0<=x<1024 or not 0<=y<600:
        raise ValueError('PANEL_CTL.COORDINATE_OUT_OF_BOUNDS')
    return round(x*32767/1023),round(y*32767/599)


def pointer_events(x:int,y:int,down:bool,multitouch:bool=False,phase:str='begin') -> list[dict]:
    x,y=coordinates(x,y)
    if multitouch:
        def event(kind,axis,value,tracking=1):
            return {'type':'mtt','data':{'type':kind,'slot':0,'tracking-id':tracking,'axis':axis,'value':value}}
        if not down:return [event('end','x',x,-1)]
        events=[event('begin','x',x)] if phase=='begin' else []
        return events+[event('data','x',x),event('data','y',y)]
    return [{'type':'abs','data':{'axis':'x','value':x}},
            {'type':'abs','data':{'axis':'y','value':y}},
            {'type':'btn','data':{'button':'left','down':down}}]


def guest_control(payload:dict,port:int=2228) -> dict:
    """Send typed JSON on stdin to a fixed guest endpoint, using private sim keys."""
    command=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10',
             '-o','StrictHostKeyChecking=accept-new','-o','UserKnownHostsFile='+str(SIM/'known_hosts'),
             '-i',str(SIM/'ssh_key'),'-p',str(port),'paneladmin@127.0.0.1',
             'sudo','python3','/opt/bhp-sim/scripts/sim/guest/control.py']
    result=subprocess.run(command,input=json.dumps(payload)+'\n',capture_output=True,text=True,timeout=45)
    if result.returncode:
        raise ValueError('PANEL_CTL.GUEST_CONTROL_FAILED: '+result.stderr[-1000:])
    response=json.loads(result.stdout)
    if response.get('error'):
        raise ValueError('PANEL_CTL.GUEST_REJECTED: '+json.dumps(response['error']))
    return response


def run_action(action:dict,execute:bool,port:int=2228) -> dict:
    """Validate and execute exactly one known simulation operation."""
    if not isinstance(action,dict):raise ValueError('PANEL_CTL.ACTION_INVALID')
    kind=action['type']
    fields={'tap':({'type','x','y'},{'multitouch'}),'swipe':({'type','x','y','end_x','end_y'},{'multitouch'}),
            'presence':({'type','present'},set()),'scenario':({'type','scenario_id'},set()),
            'screenshot':({'type','name'},set()),'status':({'type'},set()),'select_touch':({'type','protocol'},set())}
    if kind not in fields:raise ValueError('PANEL_CTL.ACTION_UNKNOWN')
    required,optional=fields[kind]
    if not required<=set(action) or set(action)-(required|optional):raise ValueError('PANEL_CTL.ACTION_FIELDS_INVALID')
    if 'multitouch' in action and type(action['multitouch']) is not bool:raise ValueError('PANEL_CTL.MULTITOUCH_INVALID')
    if kind in ('tap','swipe'):
        coordinates(action['x'],action['y'])
        if kind=='swipe': coordinates(action['end_x'],action['end_y'])
    elif kind=='screenshot':
        if re.fullmatch(r'[a-zA-Z0-9_-]{1,80}',action['name']) is None: raise ValueError('PANEL_CTL.NAME_INVALID')
    elif kind=='scenario':
        if re.fullmatch(r'[a-z0-9_-]{1,80}',action['scenario_id']) is None: raise ValueError('PANEL_CTL.SCENARIO_INVALID')
        if not (ROOT/'simulator/scenarios'/(action['scenario_id']+'.json')).is_file(): raise ValueError('PANEL_CTL.SCENARIO_UNKNOWN')
    elif kind=='presence':
        if type(action['present']) is not bool: raise ValueError('PANEL_CTL.PRESENCE_INVALID')
    elif kind=='select_touch' and action['protocol'] not in ('single','type_b'):
        raise ValueError('PANEL_CTL.TOUCH_PROTOCOL_INVALID')
    elif kind not in ('status','select_touch'):
        raise ValueError('PANEL_CTL.ACTION_UNKNOWN')
    if not execute: return {'execute':False,'action':action,'evidence':EVIDENCE}
    if kind in ('presence','scenario','status','select_touch'):
        result=guest_control(action,port)
    else:
        with QmpClient(qmp_path()) as qmp:
            if kind=='screenshot':
                directory=SIM/'screenshots'; directory.mkdir(parents=True,exist_ok=True)
                path=directory/(action['name']+'.png')
                qmp.command('screendump',{'filename':str(path),'format':'png'})
                result={'path':str(path)}
            else:
                mt=action.get('multitouch',False)
                if not mt:
                    select_tablet(qmp)
                qmp.command('input-send-event',{'events':pointer_events(action['x'],action['y'],True,mt)})
                if kind=='swipe':
                    for step in range(1,9):
                        time.sleep(0.025)
                        x=round(action['x']+(action['end_x']-action['x'])*step/8)
                        y=round(action['y']+(action['end_y']-action['y'])*step/8)
                        qmp.command('input-send-event',{'events':pointer_events(x,y,True,mt,'update')})
                else:
                    x,y=action['x'],action['y']; time.sleep(0.08)
                qmp.command('input-send-event',{'events':pointer_events(x,y,False,mt,'end')})
                result={'input_injected':True}
    emit('control_action_completed',action=action,result=result)
    return {'execute':True,'action':action,'data':result,'evidence':EVIDENCE}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--ssh-port',type=int,default=2228)
    sub=parser.add_subparsers(dest='action',required=True)
    for name in ('tap','swipe'):
        p=sub.add_parser(name); p.add_argument('x',type=int); p.add_argument('y',type=int); p.add_argument('--multitouch',action='store_true')
        if name=='swipe': p.add_argument('end_x',type=int); p.add_argument('end_y',type=int)
    sub.add_parser('status')
    sub.add_parser('presence').add_argument('state',choices=('on','off'))
    sub.add_parser('scenario').add_argument('scenario_id')
    sub.add_parser('screenshot').add_argument('name')
    sub.add_parser('select-touch').add_argument('protocol',choices=('single','type_b'))
    sub.add_parser('play').add_argument('scenario',type=Path)
    args=parser.parse_args(); values=vars(args)
    if args.action=='play':
        document=json.loads(args.scenario.read_text())
        if set(document)!= {'schema_version','events'} or document['schema_version']!=1 or not 1<=len(document['events'])<=256:
            raise ValueError('PANEL_CTL.PLAYBACK_INVALID')
        for action in document['events']:
            print(json.dumps(run_action(action,args.execute,args.ssh_port)),flush=True)
            if args.execute: time.sleep(1.1)
        return 0
    action={k:v for k,v in values.items() if k not in ('execute','ssh_port','action')}
    action['type']=args.action.replace('-','_')
    if args.action=='presence': action={'type':'presence','present':args.state=='on'}
    print(json.dumps(run_action(action,args.execute,args.ssh_port)))
    return 0

if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print(json.dumps({'error_code':'PANEL_CTL.FAILED','detail':str(error),'evidence':EVIDENCE}),file=sys.stderr)
        raise SystemExit(1)
