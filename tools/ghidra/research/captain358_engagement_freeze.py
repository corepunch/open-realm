#!/usr/bin/env python3
"""Freeze fresh Captain near-home and negative-mana public repeats/control."""
import argparse,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain358_engagement as v

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);a=p.parse_args()
 if v.SPEC.exists()or v.SPEC.with_suffix('.json.gz').exists():p.error('fresh fixture paths required')
 groups={};scenes={}
 for name in ('near','negative','lost'):
  names=[name+'-observe-1',name+'-observe-2',name+'-control-1']
  cases=[dict(rows=[json.loads(s)for s in(a.reports/(n+'.jsonl')).read_text().splitlines()],preload=(a.reports/(n+'-preload.txt')).read_bytes().decode())for n in names]
  groups[name]=cases;scenes[name]=dict(losses=v.boundaries(cases[0]['rows'],'member-removed')if name=='lost'else[],public=v.public(cases[0]['preload']),contacts=v.boundaries(cases[0]['rows']),sources=[c['rows'][0]['source_sha256']for c in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
 sources=['tools/frida/research/'+s for s in ('captain358_engagement_make_map.py','captain358_engagement_observer.js','captain358_negative_make_map.py','captain358_negative_probe.j','captain358_negative_probe.ai','captain358_observer.js','group355_capture.py','captain358_make_map.py','captain358_probe.j','captain358_probe.ai','GROUP-03.4.7.3_make_map.py')]+['tools/ghidra/research/Captain358EngagementEvidence.java','tools/frida/make_wc3_pathfinding_map.py']
 spec=dict(version=1,task='GROUP-03.4.7.3',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=dict(re.findall(r'^([0-9a-f]{8})\|([0-9a-f]+)\|',(a.reports/'engagement-static-evidence.txt').read_text(),re.M)),pins={p:v.digest((ROOT/p).read_bytes())for p in sources},scenes=scenes,scope='Near-home hostile contact retains CaptainAttack point while routing to source; signed mana counter persists across removal/contact. Source removal restores the retained route without roster mutation. Broader source policies, lifecycle ordering, full trajectories and retail save/load remain GROUP-03.4.7.3.')
 result=v.runtime(groups,spec)
 packed=gzip.compress((json.dumps(groups,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(packed)
 v.SPEC.with_suffix('.json.gz').write_bytes(packed);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
