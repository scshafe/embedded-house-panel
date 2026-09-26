#!/usr/bin/env python3
"""Render the shared LVGL scenarios and compare committed PNG baselines.

Default checks only. --update writes baselines and before/after review artifacts;
updates must be reviewed and committed. All runtime products stay in .build.
"""
from __future__ import annotations
import argparse
import html
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/sim'))
from panel_screenshots import compare_png, contact_sheet


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer',type=Path,required=True)
    parser.add_argument('--update',action='store_true')
    parser.add_argument('--output-directory',type=Path,default=ROOT/'.build/screenshots')
    args=parser.parse_args()
    goldens=ROOT/'panel_ui/tests/golden'
    output=args.output_directory.resolve(); output.mkdir(parents=True,exist_ok=True)
    manifest_path=goldens/'manifest.json'
    if args.update:
        goldens.mkdir(parents=True,exist_ok=True)
        manifest={'schema_version':1,'width':1024,'height':600,'channel_tolerance':0,'maximum_changed_pixels':0,'scenarios':[]}
    else:
        if not manifest_path.is_file():
            raise ValueError('PANEL_GOLDEN.BASELINE_MISSING: initialize and review --update explicitly')
        manifest=json.loads(manifest_path.read_text())
    scenarios=sorted((ROOT/'simulator/scenarios').glob('*.json'))
    if not scenarios:
        raise ValueError('PANEL_GOLDEN.SCENARIOS_MISSING')
    names=[path.stem for path in scenarios]
    if not args.update and names!=manifest['scenarios']:
        raise ValueError('PANEL_GOLDEN.SCENARIO_SET_MISMATCH')
    results=[]; captures=[]; before_after=[]
    for scenario in scenarios:
        name=scenario.stem; actual=output/(name+'.png'); expected=goldens/(name+'.png')
        result=subprocess.run([str(args.renderer.resolve()),'--scenario',str(scenario),'--output',str(actual)],capture_output=True,text=True,check=False)
        if result.returncode:
            results.append({'scenario':name,'passed':False,'error_code':'PANEL_GOLDEN.RENDER_FAILED','renderer_stderr':result.stderr[-2000:]})
            continue
        captures.append(actual)
        if args.update:
            if expected.exists():
                old=output/'review'/(name+'-before.png'); old.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(expected,old); before_after.append(old)
            new=output/'review'/(name+'-after.png'); new.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(actual,new); before_after.append(new)
            results.append({'scenario':name,'passed':True,'baseline_updated':True})
        elif not expected.exists():
            results.append({'scenario':name,'passed':False,'error_code':'PANEL_GOLDEN.BASELINE_MISSING'})
        else:
            results.append({'scenario':name,**compare_png(expected,actual,output/(name+'-diff.png'),manifest['channel_tolerance'],manifest['maximum_changed_pixels'])})
    passed=len(results)==len(scenarios) and all(r['passed'] for r in results)
    report={'schema_version':1,'evidence':'simulation_not_hardware_evidence','passed':passed,'baseline_update':args.update,'results':results}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if captures:
        contact_sheet(captures,output/'contact-sheet.png')
        (output/'contact-sheet.html').write_text('<!doctype html><meta charset="utf-8"><title>Panel states</title><style>body{background:#14181f;color:#eee;font:16px system-ui}main{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}img{width:100%}figure{margin:0}</style><h1>Headless LVGL states — simulation</h1><main>'+''.join('<figure><img src="'+html.escape(p.name)+'"><figcaption>'+html.escape(p.stem)+'</figcaption></figure>' for p in captures)+'</main>')
    if args.update and passed:
        for capture in captures:
            shutil.copyfile(capture,goldens/capture.name)
        manifest['scenarios']=names; manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
        if before_after:
            contact_sheet(before_after,output/'review/before-after.png',columns=2)
    print(json.dumps(report))
    return 0 if passed else 1

if __name__=='__main__':
    try:
        raise SystemExit(main())
    except (OSError,ValueError,KeyError) as error:
        print(json.dumps({'passed':False,'error_code':'PANEL_GOLDEN.FAILED','detail':str(error)}),file=sys.stderr)
        raise SystemExit(1)
