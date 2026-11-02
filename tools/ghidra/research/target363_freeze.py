#!/usr/bin/env python3
"""Freeze complete independently repeated/control mixed target transactions."""
import argparse,gzip,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target363 as v

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);a=p.parse_args()
 if v.SPEC.exists()or v.SPEC.with_suffix('.json.gz').exists():p.error('fresh fixture paths required')
 groups={};scenes={}
 for variant,observed in [('remove',(2,4)),('owner',(2,4)),('death',(1,2))]:
  names=[f'mixed-{variant}-observe-{i}'for i in observed]+[f'mixed-{variant}-control-1']
  cases=[dict(rows=[json.loads(line)for line in(a.reports/(name+'.jsonl')).read_text().splitlines()],
              preload=(a.reports/(name+'-preload.txt')).read_bytes().decode())for name in names]
  groups[variant]=cases
  scenes[variant]=dict(projection=v.trace.project(v.trace.validate_rows(cases[0]['rows']),variant),
    sources=[case['rows'][0]['source_sha256']for case in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
 sources=['tools/frida/research/'+name for name in ('target363_capture.py','target363_make_map.py','target363_death_make_map.py',
  'target363_registry_observer.js','target363_probe.j','target363_probe.ai','captain358_engagement_make_map.py',
  'captain358_make_map.py','captain358_probe.j','captain358_probe.ai','GROUP-03.4.7.3_make_map.py')]
 sources+=['tools/ghidra/research/Target363Evidence.java','tools/ghidra/research/target363_trace.py',
           'tools/extract_wc3_ability_classes.py','tools/frida/make_wc3_pathfinding_map.py']
 evidence=json.loads((a.reports/'registry-full-evidence.json').read_text())
 spec=dict(version=1,task='GROUP-03.4.7.3',game_sha256=v.SHA,engine_tests=v.TESTS,
           instructions=evidence['instructions'],pins={name:v.digest((ROOT/name).read_bytes())for name in sources},scenes=scenes,
           scope='Typed Move/Attack/Captain event FIFO, same-Captain role/roster deduplication, per-event ranks, nested mutation/growth and saved logical ownership. Three public RemoveUnit/owner-transfer/KillUnit scenes freeze complete native transactions including Inventory/Missile context. Inventory/Missile engine ordering, broader Captain engagement/default/target policies, full trajectories and retail save/load remain in existing GROUP-03.4.7.3 and BASE/E2E scopes.')
 result=v.runtime(groups,spec)
 packed=gzip.compress((json.dumps(groups,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(packed)
 v.SPEC.with_suffix('.json.gz').write_bytes(packed);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
