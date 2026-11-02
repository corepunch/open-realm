#!/usr/bin/env python3
"""Freeze only completed original ClearCaptainTargets captures and static guards."""
import argparse,gzip,json,sys,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain368_clear as v

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists()or a.output.with_suffix('.json.gz').exists():p.error('fresh fixture required')
 names={'point':['pointclear-observe-4','pointclear-observe-5','pointclear-control-1'],'follow':['clear-retained-observe-2','clear-retained-observe-3','clear-retained-control-1']}
 groups={scene:[dict(rows=[json.loads(l)for l in(a.reports/(name+'.jsonl')).read_text().splitlines()],preload=(a.reports/(name+'-preload.txt')).read_bytes().decode())for name in ns]for scene,ns in names.items()}
 scenes={scene:dict(public=v.public(cases[0]['preload']),sources=[c['rows'][0]['source_sha256']for c in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'],projection=v.projection(cases[0]['rows']))for scene,cases in groups.items()}
 instructions={m[1]:m[2]for line in(a.reports/'clear368-static-evidence.txt').read_text().splitlines()if(m:=re.match(r'^([0-9a-f]{8})\|([0-9a-f]+)\|',line))}
 pins=['tools/frida/research/captain358_clear_make_map.py','tools/frida/research/captain358_clear_observer.js','tools/frida/research/captain358_clear_probe.ai','tools/frida/research/captain358_clear_probe.j','tools/frida/research/captain358_engagement_make_map.py','tools/frida/research/captain358_make_map.py','tools/frida/research/GROUP-03.4.7.3_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/frida/research/group355_capture.py','tools/ghidra/research/Captain368ClearEvidence.java']
 bundle=gzip.compress(json.dumps(groups,separators=(',',':')).encode(),mtime=0)
 spec=dict(task='GROUP-03.4.7.3',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=instructions,scenes=scenes,pins={s:v.digest((ROOT/s).read_bytes())for s in pins},bundle_sha256=v.digest(bundle),scope='Public targetless and single-follow ClearCaptainTargets immediate synchronous callback boundaries, retained nonhome point and source inverse. Pending d01c2 target-selection cancellation, all target-role combinations, cross-owner registry and full journeys remain in existing GROUP-03.4.7.3.')
 v.runtime(groups,spec)
 if len(instructions)!=551:raise ValueError('wrong complete original guards')
 a.output.write_text(json.dumps(spec,indent=2)+'\n');a.output.with_suffix('.json.gz').write_bytes(bundle)
 print(json.dumps(dict(instructions=len(instructions),**v.runtime(groups,spec))))
if __name__=='__main__':main()
