#!/usr/bin/env python3
"""Freeze completed native query observations and uninstrumented controls."""
import argparse,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain369_selection as v

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists()or a.output.with_suffix('.json.gz').exists():p.error('fresh fixture required')
 names={'clear':['selection369-clear-observe-1','selection369-clear-observe-2','selection369-clear-control-1'],
        'reachable':['selection369-reachableplayer-observe-1','selection369-reachableplayer-observe-2','selection369-reachableplayer-control-1'],
        'candidates':['selection369-candidates-player-observe-1','selection369-candidates-player-observe-2','selection369-candidates-player-control-1'],
        'transfer':['selection369-transfer-observe-1','selection369-transfer-observe-2','selection369-transfer-control-1'],
        'loss':['selection369-loss-observe-1','selection369-loss-observe-2','selection369-loss-control-1']}
 groups={scene:[dict(rows=[json.loads(l)for l in(a.reports/(name+'.jsonl')).read_text().splitlines()],preload=(a.reports/(name+'-preload.txt')).read_bytes().decode())for name in ns]for scene,ns in names.items()}
 scenes={scene:dict(public=v.public(cases[0]['preload']),sources=[c['rows'][0]['source_sha256']for c in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'],projection=v.projection(cases[0]['rows']))for scene,cases in groups.items()}
 instructions={}
 for name in('selection369-static-evidence.txt','selection369-policy-static-evidence.txt','selection369-selector-static-evidence.txt','selection369-random-paths-static-evidence.txt','selection369-primary-loss-static-evidence.txt'):
  for line in(a.reports/name).read_text().splitlines():
   if(m:=re.match(r'^([0-9a-f]{8})\|([0-9a-f]+)\|',line)):
    if m[1]in instructions and instructions[m[1]]!=m[2]:raise ValueError('overlapping instructions differ')
    instructions[m[1]]=m[2]
 pins=['tools/frida/research/captain369_make_map.py','tools/frida/research/captain369_probe.ai','tools/frida/research/captain369_probe.j','tools/frida/research/captain369_observer.js','tools/frida/research/captain369_candidates_make_map.py','tools/frida/research/captain369_candidates_probe.ai','tools/frida/research/captain369_candidates_probe.j','tools/frida/research/captain369_candidates_observer.js','tools/frida/research/captain369_transfer_make_map.py','tools/frida/research/captain369_transfer_probe.ai','tools/frida/research/captain369_transfer_probe.j','tools/frida/research/group355_capture.py','tools/ghidra/research/Captain369SelectionEvidence.java','tools/ghidra/research/Captain369SelectionPolicyEvidence.java','tools/ghidra/research/Captain369SelectorEvidence.java','tools/ghidra/research/Captain369RandomPathsEvidence.java','tools/ghidra/research/Captain369PrimaryLossEvidence.java','tools/frida/research/captain369_loss_make_map.py','tools/frida/research/captain369_loss_probe.ai','tools/frida/research/captain369_loss_probe.j']
 bundle=gzip.compress(json.dumps(groups,separators=(',',':')).encode(),mtime=0)
 spec=dict(task='GROUP-03.4.7.3',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=instructions,scenes=scenes,pins={s:v.digest((ROOT/s).read_bytes())for s in pins},bundle_sha256=v.digest(bundle),scope='Independent +98 target selection versus +84 periodic work, immediate Clear cancellation, deferred reachable ordinary target, native seven-candidate FIFO budget and transfer-stable class query order; pending-query cold save and selected-primary loss restore/rearm in engine. Suicide ranking publication, random structure destinations, transport and complete journeys remain in existing GROUP-03.4.7.3.')
 v.runtime(groups,spec);a.output.write_text(json.dumps(spec,indent=2)+'\n');a.output.with_suffix('.json.gz').write_bytes(bundle)
 print(json.dumps(dict(instructions=len(instructions),**v.runtime(groups,spec))))
if __name__=='__main__':main()
