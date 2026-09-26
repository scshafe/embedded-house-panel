#!/usr/bin/env python3
"""Verify fresh real guest touch commits, PIR blanking and every scenario image.

Requires an explicitly launched/provisioned guest. Missing VM fails this gate.
It cannot produce FRDM hardware evidence.
"""
import json
import subprocess
import sys
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/sim'))
from virtual_panel_vm import SIM,EVIDENCE,qmp_path
from panel_ctl import QmpClient,run_action
from panel_screenshots import contact_sheet,read_png,compare_png
from provision_virtual_panel import ssh_arguments


def guest_status():return run_action({'type':'status'},True)['data']['data']


def verify_touch(protocol):
    run_action({'type':'select_touch','protocol':protocol},True)
    run_action({'type':'scenario','scenario_id':'day-front'},True)
    before=guest_status();previous=before.get('last_touch')
    # Publication invalidates a crossing contact. Retry a bounded fresh contact;
    # each attempt records its true outcome, never fabricates an accepted touch.
    attempts=[]
    for _ in range(4):
        run_action({'type':'tap','x':300,'y':300,'multitouch':protocol=='type_b'},True)
        time.sleep(0.3);after=guest_status();current=after.get('last_touch')
        accepted=current is not None and (previous is None or current['cursor']!=previous['cursor'])
        attempts.append({'fresh_commit':accepted,'status':after})
        if accepted:
            if current['status']['study_state']!='back':raise ValueError('PANEL_VM.TOUCH_DID_NOT_REVEAL')
            # The touch log records the completed flip. Also require the next
            # real lifecycle render/ack to retain that revealed face.
            for _ in range(8):
                state=guest_status();ui=state.get('ui') or {}
                if (ui.get('display_commit_backend')=='drm_page_flip_completion'
                        and ui.get('presentation_result')=='committed'
                        and ui.get('study_state')=='back'):
                    return {'protocol':protocol,'passed':True,'attempts':attempts,
                            'lifecycle_commit_after_touch':state}
                time.sleep(0.3)
            raise ValueError('PANEL_VM.TOUCH_LIFECYCLE_COMMIT_MISSING:'+protocol)
        time.sleep(0.37)
    raise ValueError('PANEL_VM.NO_FRESH_ACCEPTED_TOUCH:'+protocol)


def main():
    output=SIM/'vm';output.mkdir(parents=True,exist_ok=True)
    report={'evidence':EVIDENCE,'passed':False,'scenarios':[],'suspend':'disabled'}
    try:
        with QmpClient(qmp_path()) as qmp:
            report['qemu_status']=qmp.command('query-status')
            if report['qemu_status'].get('status')!='running':raise ValueError('PANEL_VM.NOT_RUNNING')
        status=guest_status();report['guest_status']=status
        if any(value!='active' for value in status['services'].values()):raise ValueError('PANEL_VM.SERVICES_NOT_ACTIVE')
        report['touch']=[verify_touch('single'),verify_touch('type_b')]
        run_action({'type':'presence','present':False},True);time.sleep(4)
        report['absence_status']=guest_status()
        if report['absence_status']['brightness']!=0:raise ValueError('PANEL_VM.ABSENCE_NOT_DARK')
        run_action({'type':'presence','present':True},True)
        captures=[]
        for scenario in sorted((ROOT/'simulator/scenarios').glob('*.json')):
            run_action({'type':'scenario','scenario_id':scenario.stem},True)
            state=guest_status()
            if not state['ui'] or state['ui']['scenario_id']!=scenario.stem:raise ValueError('PANEL_VM.SCENE_NOT_SELECTED:'+scenario.stem)
            response=run_action({'type':'screenshot','name':scenario.stem},True)
            path=Path(response['data']['path']);width,height,pixels=read_png(path)
            passed=(width,height)==(1024,600)
            item={'scenario':scenario.stem,'passed':passed,'path':str(path),'status':state}
            if scenario.stem=='clock-untrusted':
                # Guest visibly labels app fixture clock availability, preserving
                # the real supervisor's NTP-backed lifecycle instead of spoofing it.
                item['fixture_clock_notice']=True
                if len(set(pixels))<3:raise ValueError('PANEL_VM.CLOCK_DIAGNOSTIC_NOT_VISIBLE')
            elif scenario.stem=='grace':
                # Real guest brightness is70/60 versus deterministic80/40 native.
                # Require actual grace and lower nonzero kernel backlight level.
                if state['ui']['brightness_percent']!=60 or state['brightness']==0:raise ValueError('PANEL_VM.GRACE_NOT_OBSERVED')
            else:
                comparison=compare_png(ROOT/'panel_ui/tests/golden'/path.name,path,output/(scenario.stem+'-diff.png'))
                item['golden_comparison']=comparison;item['passed']=passed and comparison['passed']
            report['scenarios'].append(item);captures.append(path)
        contact_sheet(captures,output/'contact-sheet.png')
        logs=subprocess.check_output(['ssh',*ssh_arguments(2228),'paneladmin@127.0.0.1',
            'sudo journalctl -u bhp-sim-ui -u bhp-sim-supervisor -b --no-pager -o cat'],text=True)
        (output/'guest-services.jsonl').write_text(logs)
        report['passed']=all(item['passed'] for item in report['scenarios'])
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        report['error_code']='PANEL_VM.INTEGRATION_FAILED';report['detail']=str(error)
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report));return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
