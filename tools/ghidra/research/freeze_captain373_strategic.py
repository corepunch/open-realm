#!/usr/bin/env python3
"""Freeze native graph, hidden-source and ShowUnit/invisibility query evidence."""
import argparse,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain373_strategic as v

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);p.add_argument('--continuation-reports',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists()or a.output.with_suffix('.json.gz').exists():p.error('fresh output required')
 names={'regions':['selection369-regions-input-observe-1','selection369-regions-input-observe-2','selection369-regions-control-1'],'sources':['sources373-observe-3','sources373-observe-5','sources373-control-2'],'coexistence':['coexistence373-observe-1','coexistence373-observe-2','coexistence373-control-1'],'notification':['coexistence373-notify-observe-1']}
 bundle={}
 for scene,ns in names.items():
  reports=a.reports if scene=='regions'else a.continuation_reports
  bundle[scene]=[dict(rows=[json.loads(l)for l in(reports/(n+'.jsonl')).read_text().splitlines()],preload=(reports/(n+'-preload.txt')).read_bytes().decode())for n in ns]
 instructions={}
 for reports,name in [(a.reports,n)for n in('selection369-static-evidence.txt','selection369-random-paths-static-evidence.txt','selection369-strategic-regions-static-evidence.txt')]+[(a.continuation_reports,'world-query-pre212-static-evidence.txt')]:
  for line in(reports/name).read_text().splitlines():
   if m:=re.match(r'^([0-9a-f]{8})\|([0-9a-f]+)\|',line):
    if m[1]in instructions and instructions[m[1]]!=m[2]:raise ValueError('overlapping guards differ')
    instructions[m[1]]=m[2]
 for line in(a.reports/'selection369-region-tables.txt').read_text().splitlines():
  address,data=line.split('|');instructions[address]=data
 oracle_path='tools/ghidra/fixtures/retail-captain369-regions-1.27.json.gz';oracle=json.loads(gzip.decompress((ROOT/oracle_path).read_bytes()))
 pins=['tools/frida/research/captain369_regions_observer.js','tools/frida/research/captain369_regions_input_observer.js','tools/frida/research/captain369_candidates_make_map.py','tools/frida/research/captain369_candidates_probe.ai','tools/frida/research/captain369_candidates_probe.j','tools/frida/research/group355_capture.py','tools/ghidra/research/Captain369StrategicRegionsEvidence.java','tools/ghidra/research/Captain369ExportRegionTables.java','tools/ghidra/research/Captain373WorldQueryEvidence.java','tools/ghidra/research/captain369_regions_model.py','tools/ghidra/research/captain369_regions_c_model.py','tools/ghidra/research/captain369_regions_c_model.c','tools/ghidra/research/freeze_captain369_regions.py','tools/ghidra/research/emit_captain369_regions_test.py','tools/ghidra/research/freeze_captain373_strategic.py','games/warcraft-3/common/wc3_ai_regions.h','games/warcraft-3/game/tests/retail_captain369_regions.h']
 for task in('sources','coexistence'):
  pins.extend('tools/frida/research/captain373_'+task+suffix for suffix in('_make_map.py','_probe.ai','_probe.j','_observer.js'))
 pins.append('tools/frida/research/captain373_coexistence_notify_observer.js')
 data=gzip.compress(json.dumps(bundle,separators=(',',':')).encode(),mtime=0)
 spec=dict(task='GROUP-03.4.7.3',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=instructions,table_guards=5,oracle=oracle_path,oracle_sha256=v.digest((ROOT/oracle_path).read_bytes()),bundle_sha256=v.digest(data),runtime_sources={scene:[c['rows'][0]['source_sha256']for c in cases]for scene,cases in bundle.items()},scenes={scene:dict(public=v.public(cases[0]['preload']),projection=v.projection(cases[0]['rows'],scene))for scene,cases in bundle.items()},pins={s:v.digest((ROOT/s).read_bytes())for s in pins},scope='SuicidePlayer deferred publication; exact native strategic-region kernel and production cached-hierarchy/publication-boundary replay; physical ShowUnit retirement versus active WindWalk invisibility, owner inverse and cold save. Native query flags/notification chronology retain actual public producer proof. Candidate ranking is restored at the captured graph boundary; complete same-map journeys, transport, SuicidePlayerUnits and Town preferred-primary remain outside this gate in GROUP-03.4.7.3.')
 result=v.runtime(spec,bundle,oracle);a.output.write_text(json.dumps(spec,indent=2)+'\n');a.output.with_suffix('.json.gz').write_bytes(data);print(json.dumps(dict(instructions=len(instructions),**result)))
if __name__=='__main__':main()
