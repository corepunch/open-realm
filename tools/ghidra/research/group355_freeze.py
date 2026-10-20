#!/usr/bin/env python3
"""Freeze completed Group355 observations without deriving any engine expectations."""
import argparse,base64,gzip,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_group355 as v

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True);a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('fresh fixtures required')
    cases=[]
    for name in('observe-d','observe-e','control-b'):
        cases.append(dict(rows=[json.loads(l)for l in(a.archive/(name+'.jsonl')).read_text().splitlines()],
                          preload=(a.archive/(name+'-preload.txt')).read_bytes().decode()))
    meta=json.loads((a.archive/'Group355b.json').read_text())
    base=a.archive.parent/'payoff338/verified/Group338Hard.w3m'
    if v.digest(base.read_bytes())!=meta['base_sha256']:raise ValueError('frozen base differs')
    cases[0]['map_input']=base64.b64encode(base.read_bytes()).decode()
    # The previous verified observer projection only removes process identity;
    # every simulation/event word, reference equality and event ordering remains.
    sources=['tools/frida/research/group355_probe.j','tools/frida/research/group355_observer.js',
             'tools/frida/research/group355_capture.py','tools/frida/research/group355_make_map.py',
             'tools/ghidra/verify_wc3_pathing_group341.py','tools/ghidra/research/Group355Evidence.java']
    spec=dict(version=1,task='GROUP-03.2',game_sha256=v.SHA,engine_tests=v.TESTS,
              pins={s:v.digest((v.ROOT/s).read_bytes())for s in sources},
              capture_sources=[c['rows'][0]['source_sha256']for c in cases],
              base_sha256=meta['base_sha256'],map_sha256=v.digest((a.archive/'Group355b.w3m').read_bytes()),
              public=v.public(cases[0]['preload']),events=v.projection(cases[0]['rows']),
              native_events=len(v.projection(cases[0]['rows'])),
              instructions=json.loads((a.archive/'guards-static.json').read_text())['instructions'],
              limits=['No full-frame or full-trajectory parity claim.',
                      'Retained minimum-range priority is distinct from in-range stickiness and minimum-range reach admission.',
                      'Other ability override classes and general GROUP-03.2 remain open.'])
    v.runtime(cases,spec)
    v.BUNDLE.write_bytes(gzip.compress((json.dumps(cases,separators=(',',':'))+'\n').encode(),mtime=0))
    spec['bundle_sha256']=v.digest(v.BUNDLE.read_bytes());v.SPEC.write_text(json.dumps(spec,indent=2)+'\n')
    print(json.dumps(v.runtime(v.validate(spec),spec)))
if __name__=='__main__':main()
