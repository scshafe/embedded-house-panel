#!/usr/bin/env python3
"""Capture every scenario through a real native SDL window; never a hardware claim."""
from __future__ import annotations
import argparse,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/sim'))
from panel_screenshots import contact_sheet,read_png

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
        if passed:
            width,height,_=read_png(image); passed=(width,height)==(1024,600); images.append(image)
        records.append({'scenario':scenario.stem,'passed':passed,'returncode':result.returncode,
                        'output':result.stdout[-4000:],'stderr':result.stderr[-1000:]})
    report={'evidence':'simulation_not_hardware_evidence','passed':all(r['passed'] for r in records),'scenarios':records}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if images: contact_sheet(images,output/'contact-sheet.png')
    print(json.dumps(report))
    return 0 if report['passed'] else 1
if __name__=='__main__': raise SystemExit(main())
