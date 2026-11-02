#!/usr/bin/env python3
"""Freeze original public Sleep/stock attachment evidence before engine implementation."""
import argparse,base64,gzip,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_group367 as v

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True);a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('fresh fixtures required')
    base=a.archive.parent/'payoff338/verified/Group338Hard.w3m';experiments=[];contracts=[]
    for prefix,mapname,probe,names in [('G367','Group367c','group367_probe.j',('observe-c','observe-d','control-a')),('P367','Sleep367','group367_sleep_probe.j',('sleep-observe-a','sleep-observe-b','sleep-control-a')),('N367','Sleep367Nested','group367_nested_probe.j',('nested-observe-c','nested-observe-d','nested-control-b')),('F367','Group367Factory','group367_factory_probe.j',('factory-observe-a','factory-observe-b','factory-control-a')),('R367','Group367Refresh','group367_refresh_probe.j',('refresh-canonical-a','refresh-canonical-b','refresh-canonical-control'))]:
        cases=[dict(rows=[json.loads(l)for l in(a.archive/(n+'.jsonl')).read_text().splitlines()],preload=(a.archive/(n+'-preload.txt')).read_bytes().decode())for n in names]
        meta=json.loads((a.archive/(mapname+'.json')).read_text())
        if v.digest(base.read_bytes())!=meta['base_sha256']:raise ValueError('wrong map base')
        experiments.append(dict(cases=cases,map_input=base64.b64encode(base.read_bytes()).decode()))
        contracts.append(dict(prefix=prefix,probe='tools/frida/research/'+probe,map_sha256=meta['map_sha256'],base_sha256=meta['base_sha256'],capture_sources=[c['rows'][0]['source_sha256']for c in cases],public=v.public(cases[0]['preload'],prefix),events=v.projection(cases[0]['rows'])))
    contracts[3]['builder']='tools/frida/research/group367_factory_make_map.py'
    contracts[4]['builder']='tools/frida/research/group367_refresh_make_map.py'
    paths=['tools/frida/research/group367_refresh_probe.j','tools/frida/research/group367_refresh_make_map.py','tools/frida/research/group367_refresh_canonical_observer.js','tools/ghidra/research/Group367ClockEvidence.java','tools/frida/research/group367_factory_probe.j','tools/frida/research/group367_factory_observer.js','tools/frida/research/group367_factory_make_map.py','tools/frida/research/separation366_capture.py','tools/frida/research/group367_nested_probe.j','tools/frida/research/group367_nested_typed_observer.js','tools/ghidra/research/Group367SuspensionEvidence.java','tools/ghidra/research/group367_typed.py','tools/frida/research/group367_probe.j','tools/frida/research/group367_sleep_probe.j','tools/frida/research/group367_observer.js','tools/frida/research/group367_sleep_observer.js','tools/frida/research/group355_capture.py','tools/frida/research/group341_make_map.py','tools/frida/research/group338_probe.j','tools/ghidra/verify_wc3_pathing_group359.py','tools/ghidra/verify_wc3_pathing_group341.py','tools/ghidra/research/Group367Evidence.java']
    instructions=json.loads((a.archive/'static-sleep.json').read_text())['instructions']
    for k,value in json.loads((a.archive/'static-nested.json').read_text())['instructions'].items():
        if k in instructions and instructions[k]!=value:raise ValueError('overlapping guard differs')
        instructions[k]=value
    for k,value in json.loads((a.archive/'static-clock-complete2.json').read_text())['instructions'].items():
        if k in instructions and instructions[k]!=value:raise ValueError('clock guard differs')
        instructions[k]=value
    data_guards={'6fcd539c':instructions.pop('6fcd539c')}
    spec=dict(version=1,task='GROUP-03.2',game_sha256=v.SHA,engine_tests=v.TESTS,functions=63,data_guards=data_guards,pins={p:v.digest((v.ROOT/p).read_bytes())for p in paths},experiments=contracts,instructions=instructions,limits=['Concrete Stun/SleepStun producer reachability and other GROUP-03.2 guards remain in this existing leaf.','Stock attachment rejection does not prove custom-data or factory unreachability.','No full-frame, arbitrary trajectory or retail-save parity claim.'])
    v.runtime(experiments,spec)
    v.BUNDLE.write_bytes(gzip.compress((json.dumps(experiments,separators=(',',':'))+'\n').encode(),mtime=0));spec['bundle_sha256']=v.digest(v.BUNDLE.read_bytes());v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(v.runtime(v.validate(spec),spec)))
if __name__=='__main__':main()
