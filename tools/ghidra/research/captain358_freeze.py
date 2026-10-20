#!/usr/bin/env python3
"""Freeze completed Captain358 public repeats/control; refuse partial captures."""
import argparse,gzip,hashlib,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain358 as verify

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);a=p.parse_args()
 specpath=verify.SPEC
 if specpath.exists()or specpath.with_suffix('.json.gz').exists():p.error('fresh fixture paths required')
 cases=[]
 for name in ('observe-3','observe-4','control-3'):
  cases.append(dict(rows=[json.loads(s)for s in (a.reports/(name+'.jsonl')).read_text().splitlines()],preload=(a.reports/(name+'-preload.txt')).read_bytes().decode()))
 instructions=dict(re.findall(r'^([0-9a-f]{8})\|([0-9a-f]+)\|',(a.reports/'static-evidence.txt').read_text(),re.M))
 sources=['tools/frida/research/captain358_'+s for s in ('make_map.py','probe.j','probe.ai','observer.js')]+['tools/ghidra/research/Captain358Evidence.java','tools/frida/research/group355_capture.py']
 spec=dict(version=1,task='GROUP-03.4.7.3',game_sha256=verify.SHA,engine_tests=verify.TESTS,instructions=instructions,pins={p:verify.digest((ROOT/p).read_bytes())for p in sources},map_sha256=cases[0]['rows'][0]['source_sha256']['map'],capture_sources=[c['rows'][0]['source_sha256']for c in cases],public=verify.public(cases[0]['preload']),contact=verify.contact(cases[0]['rows']),scope='Combat contact of mana-only melee roster: retained actor home500, no retreat, engine public-frame/save/inverse. Broader combat targets, complete trajectories, other roster policy domains and retail save/load remain GROUP-03.4.7.3.')
 verify.runtime(cases,spec)
 packed=gzip.compress((json.dumps(cases,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=verify.digest(packed)
 specpath.with_suffix('.json.gz').write_bytes(packed);specpath.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(dict(fixture=str(specpath),instructions=len(instructions),**verify.runtime(cases,spec))))
if __name__=='__main__':main()
