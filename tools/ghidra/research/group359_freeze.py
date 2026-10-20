#!/usr/bin/env python3
"""Freeze complete retail counter/order observations, without deriving engine expectations."""
import argparse,base64,gzip,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_group359 as v

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--archive',type=Path,required=True);parser.add_argument('--binary',type=Path,required=True);a=parser.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():parser.error('fresh fixtures required')
    base=a.archive.parent/'payoff338/verified/Group338Hard.w3m';experiments=[];contracts=[]
    for prefix,mapname,probe,names in [('G359','Group359f','group359_probe.j',('observe-g','observe-h','control-f')),('O359','Orders359b','group359_orders_probe.j',('orders-observe-b','orders-observe-c','orders-control-b')),('O359','Death359','group359_death_probe.j',('death-observe-a','death-observe-b','death-control-b'))]:
        cases=[dict(rows=[json.loads(l)for l in(a.archive/(name+'.jsonl')).read_text().splitlines()],preload=(a.archive/(name+'-preload.txt')).read_bytes().decode())for name in names]
        meta=json.loads((a.archive/(mapname+'.json')).read_text())
        if v.digest(base.read_bytes())!=meta['base_sha256']:raise ValueError('wrong original map base')
        experiments.append(dict(cases=cases,map_input=base64.b64encode(base.read_bytes()).decode()))
        contracts.append(dict(prefix=prefix,probe='tools/frida/research/'+probe,map_sha256=meta['map_sha256'],base_sha256=meta['base_sha256'],capture_sources=[c['rows'][0]['source_sha256']for c in cases],public=v.public(cases[0]['preload'],prefix),events=v.projection(cases[0]['rows'])))
    sources=['tools/frida/research/group359_death_probe.j','tools/frida/research/group359_probe.j','tools/frida/research/group359_orders_probe.j','tools/frida/research/group359_observer.js','tools/frida/research/group355_capture.py','tools/frida/research/group341_make_map.py','tools/frida/research/group338_probe.j','tools/ghidra/verify_wc3_pathing_group341.py','tools/ghidra/research/Group359Evidence.java','tools/ghidra/research/group359_classes.py','tools/extract_wc3_ability_classes.py']
    spec=dict(version=1,task='GROUP-03.2',game_sha256=v.SHA,engine_tests=v.TESTS,pins={s:v.digest((v.ROOT/s).read_bytes())for s in sources},experiments=contracts,classes=v.original_classes(a.binary),instructions=json.loads((a.archive/'guards-phase-requirements.json').read_text())['instructions'],limits=['No full-frame or arbitrary trajectory parity claim.','CBuffSleepStun and CBuffStun public reachability, additional acquisition guards and general GROUP-03.2 remain in that existing leaf.','The class inventory distinguishes primary CAbility descendants from secondary inheritance and unrelated named types.'])
    v.runtime(experiments,spec)
    v.BUNDLE.write_bytes(gzip.compress((json.dumps(experiments,separators=(',',':'))+'\n').encode(),mtime=0));spec['bundle_sha256']=v.digest(v.BUNDLE.read_bytes());v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(v.runtime(v.validate(spec),spec)))
if __name__=='__main__':main()
