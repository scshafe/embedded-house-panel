#!/usr/bin/env python3
"""Capture every scenario through a real native SDL window; never a hardware claim."""
from __future__ import annotations
import argparse,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/sim'))
from panel_screenshots import contact_sheet,read_png,compare_png

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--simulator',required=True,type=Path)
    args=parser.parse_args(); output=ROOT/'.build/sim/native'; output.mkdir(parents=True,exist_ok=True)
    records=[]; images=[]
    for scenario in sorted((ROOT/'simulator/scenarios').glob('*.json')):
        image=output/(scenario.stem+'.png')
        command=[str(args.simulator.resolve()),'--scenario',str(scenario),'--exit-after-ms','250','--screenshot',str(image)]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=30)
        passed=result.returncode==0 and image.is_file()
        comparison=None
        if passed:
            width,height,_=read_png(image); images.append(image)
            comparison=compare_png(ROOT/'panel_ui/tests/golden'/image.name,image,
                                   output/(scenario.stem+'-diff.png'))
            passed=(width,height)==(1024,600) and comparison['passed']
        records.append({'scenario':scenario.stem,'passed':passed,'returncode':result.returncode,
                        'golden_comparison':comparison,
                        'output':result.stdout[-4000:],'stderr':result.stderr[-1000:]})
    grid=[]
    catalog=ROOT/'simulator/tests/fixtures/application_grid/seven_apps.json'
    for page in (1,2):
        event='' if page==1 else json.dumps({'type':'intent','intent_id':'shell.apps_next'})+'\n'
        expected=output/f'grid-page-{page}-reference.png';actual=output/f'grid-page-{page}.png'
        common=['--scenario',str(ROOT/'simulator/scenarios/home.json'),'--launcher-config',str(catalog),'--exit-after-ms','250']
        subprocess.run([str(args.simulator.resolve().parent/'bhp-panel-render'),*common,'--output',str(expected)],input=event,capture_output=True,text=True,check=True,timeout=30)
        subprocess.run([str(args.simulator.resolve()),*common,'--screenshot',str(actual)],input=event,capture_output=True,text=True,check=True,timeout=30)
        grid.append({'page':page,**compare_png(expected,actual,output/f'grid-page-{page}-diff.png')})
    report={'application_grid':grid,'evidence':'simulation_not_hardware_evidence','passed':all(r['passed'] for r in records+grid),'scenarios':records}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if images: contact_sheet(images,output/'contact-sheet.png')
    print(json.dumps(report))
    return 0 if report['passed'] else 1
if __name__=='__main__': raise SystemExit(main())
