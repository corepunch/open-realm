#!/usr/bin/env python3
"""Freeze fresh repeats only when they match the retained pre-implementation witness."""
import argparse,base64,gzip,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_ethereal357 as v
R=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--observe',type=Path,required=True);p.add_argument('--repeat',type=Path,required=True)
    p.add_argument('--control',type=Path,required=True)
    p.add_argument('--lifetime-observe',type=Path,required=True);p.add_argument('--lifetime-repeat',type=Path,required=True)
    p.add_argument('--lifetime-control',type=Path,required=True);a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('frozen outputs must be new')
    old=list(map(json.loads,(R/'morph333-working/morph333c-observe1.jsonl').read_text().splitlines()))
    marks=v.public((R/'morph333-working/morph333c-control1-preload.txt').read_text())
    cases=[]
    for path in [a.observe,a.repeat,a.control]:
        cases.append(dict(rows=list(map(json.loads,path.read_text().splitlines())),preload=path.with_name(path.stem+'-preload.txt').read_bytes().decode()))
    static=json.loads((R/'ethereal357/static.json').read_text())
    base=(R/'payoff323-working/evidence/Separation323g.w3m').read_bytes()
    lifetime_cases=[]
    for path in [a.lifetime_observe,a.lifetime_repeat,a.lifetime_control]:
        lifetime_cases.append(dict(rows=list(map(json.loads,path.read_text().splitlines())),preload=path.with_name(path.stem+'-preload.txt').read_bytes().decode()))
    bundle=dict(map_input=base64.b64encode(base).decode(),cases=cases,lifetime_cases=lifetime_cases)
    spec=dict(version=2,task='SEP-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,
        map_sha256='ed3594fb40bdcdf876e6091a5191fe85757d061278148a51bb0c0fa291500283',
        base_sha256=v.digest(base),public=marks,events=v.projection(old),instructions=static['instructions'],
        capture_sources=[c['rows'][0]['source_sha256']for c in cases])
    spec['lifetime']=dict(public=__import__('re').findall(r'call Preload\( "(L357 [^"\r\n]*)" \)',lifetime_cases[0]['preload']),
        events=v.projection(lifetime_cases[0]['rows']),capture_sources=[c['rows'][0]['source_sha256']for c in lifetime_cases],
        map_sha256=lifetime_cases[0]['rows'][0]['source_sha256']['map'])
    v.lifetime_runtime(lifetime_cases,spec)
    v.runtime(cases,spec)
    sources=['tools/frida/research/morph333_observer.js','tools/frida/research/morph333_probe.j',
        'tools/frida/research/ethereal357_lifetime_make_map.py','tools/frida/research/ethereal357_lifetime_probe.j',
        'tools/frida/research/morph330_make_map.py','tools/frida/research/group355_capture.py',
        'tools/ghidra/research/Ethereal357Evidence.java','tools/ghidra/research/ethereal357_freeze.py',
        'tools/ghidra/verify_wc3_pathing_ethereal357.py']
    spec['pins']={name:v.digest((ROOT/name).read_bytes())for name in sources}
    frozen=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0)
    spec['bundle_sha256']=v.digest(frozen)
    v.BUNDLE.write_bytes(frozen);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n')
    v.validate(spec);print(v.runtime(cases,spec))
if __name__=='__main__':main()
