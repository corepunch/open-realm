#!/usr/bin/env python3
"""Freeze original pre-implementation Submerge repeats and unhooked controls."""
import argparse,base64,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_submerge360 as v
from verify_wc3_pathing_ethereal357 import projection
R=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['observe','repeat','control','zero-observe','zero-repeat','zero-control']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('frozen outputs must be new')
    base=(R/'payoff323-working/evidence/Separation323g.w3m').read_bytes();bundle=dict(map_input=base64.b64encode(base).decode())
    spec=dict(version=1,task='SEP-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,base_sha256=v.digest(base),
              instructions=json.loads((R/'submerge360/static.json').read_text())['instructions'])
    for name,paths,prefix in [('stock',[a.observe,a.repeat,a.control],'M360'),('zero',[a.zero_observe,a.zero_repeat,a.zero_control],'Z360')]:
        cases=[dict(rows=[json.loads(x)for x in path.read_text().splitlines()],preload=path.with_name(path.stem+'-preload.txt').read_bytes().decode())for path in paths]
        bundle[name]=cases;spec[name]=dict(public=re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',cases[0]['preload']),
          events=projection(cases[0]['rows']),capture_sources=[c['rows'][0]['source_sha256']for c in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
    print(v.runtime(bundle,spec))
    sources=['tools/frida/research/submerge360_make_map.py','tools/frida/research/submerge360_probe.j',
      'tools/frida/research/submerge360_zero_probe.j','tools/frida/research/submerge360_observer.js',
      'tools/frida/research/group355_capture.py','tools/ghidra/research/Submerge360Evidence.java',
      'tools/ghidra/research/submerge360_freeze.py','tools/ghidra/verify_wc3_pathing_submerge360.py',
      'tools/ghidra/verify_wc3_pathing_ethereal357.py']
    spec['pins']={name:v.digest((ROOT/name).read_bytes())for name in sources}
    frozen=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(frozen)
    v.BUNDLE.write_bytes(frozen);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');v.validate(spec)
if __name__=='__main__':main()
