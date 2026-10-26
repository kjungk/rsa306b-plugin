#!/usr/bin/env python3
"""Compare the native RF factory model to hash-verified Windows API references.

Input is the accepted verify_calibration_reference report and extracted block
files. This performs no USB access and certifies numerical agreement only.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--evidence-root',type=Path,default=Path.cwd(),help='Resolve relative capture paths from this source checkout')
    p.add_argument('--tables',type=Path,required=True)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('Output must be new')
    report=json.loads(a.report.read_text())
    if not report['numerical_match']:p.error('Reference comparison must have passed')
    cases=[];lines=[]
    for run in report['runs']:
        directory=Path(run['run'])
        if not directory.is_absolute():directory=a.evidence_root/directory
        if digest(directory/'capture.json')!=run['manifest_sha256']:p.error('Changed capture manifest')
        manifest=json.loads((directory/'capture.json').read_text())
        path=directory/'api/spectra.jsonl'
        if digest(path)!=manifest['files_sha256']['api/spectra.jsonl']:p.error('Changed API record')
        spectra={s['device_timestamp_ticks']:s for s in map(json.loads,path.read_text().splitlines())}
        for block in (2,3,4,5,6):
            if digest(a.tables/f'block-{block}.bin')!=run['calibration_blocks_sha256'][str(block)]:p.error('Changed calibration tables')
        for case in run['records']:
            center=case['requested_center_hz']
            first_if=0 if case['lf_path'] else (2440e6 if center<1850e6 or 4175e6<=center<4225e6 else 1190e6)
            high=not (4175e6<=center<4225e6 or center>=4750e6)
            lines.append(' '.join(map(str,(center,case['tuned_center_hz'],case['rf_register'],case['attenuation_db'],case['if_gain_db'],first_if,int(high),case['lf_code'] or 199))))
            cases.append((case,spectra[case['api_timestamp_ticks']]['if_scaling_reference']))
    completed=subprocess.run([str(a.binary.resolve()),str(a.tables)],input='\n'.join(lines)+'\n',text=True,capture_output=True,check=True,timeout=60)
    native=list(map(json.loads,completed.stdout.splitlines()))
    if len(native)!=len(cases):p.error('Missing native results')
    results=[]
    for (case,ref),actual in zip(cases,native):
        if len(actual['eq_amplitude_db'])!=len(ref['eq_amplitude_db']):p.error('Wrong correction grid')
        scale=abs(20*math.log10(actual['volts_per_adc_count']/ref['volts_per_adc_count']))
        eq=max(abs(x-y) for x,y in zip(actual['eq_amplitude_db'],ref['eq_amplitude_db']))
        if not math.isfinite(scale+eq):p.error('Non-finite comparison')
        results.append(dict(requested_center_hz=case['requested_center_hz'],lf_path=case['lf_path'],scale_error_db=scale,eq_error_db=eq))
    scale=max(r['scale_error_db'] for r in results);eq=max(r['eq_error_db'] for r in results)
    output=dict(numerical_match=scale<1e-5 and all(r['eq_error_db']<(1e-6 if r['lf_path'] else 2e-4) for r in results),absolute_accuracy_verified=False,
        reference_report_sha256=digest(a.report),binary_sha256=digest(a.binary),
        spectra=len(results),scale_max_error_db=scale,eq_max_error_db=eq,results=results)
    with a.output.open('x') as f:json.dump(output,f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps({k:v for k,v in output.items() if k!='results'}))
    return 0 if output['numerical_match'] else 1

if __name__=='__main__':raise SystemExit(main())
