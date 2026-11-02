#!/usr/bin/env python3
"""Verify original structure target task selection and saved physical execution."""
import argparse,gzip,hashlib,json,os,re,subprocess,xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-follow278-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS=['wc3_movement.follow278*','wc3_unit.follow_stop_range_uses_misc_data_not_acquisition_range','wc3_unit.smart_follow_building_uses_zero_range_physical_task','wc3_movement.target166*','wc3_movement.follow186*','wc3_movement.refresh274*','wc3_save.rejects_prior_save_versions','wc3_save.rejects_invalid_physical_group_payloads']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(F278 [^"\r\n]*)" \)',text)
def decisions(rows):
 result=[]
 for scene in range(6):
  selected=[r for r in rows if r.get('scene')==scene]
  command=851971 if scene==2 else 851986
  heads=[r for r in selected if r['event']=='dispatch-head'and r['order']['command']==command]
  tasks=[r for r in selected if r['event']=='target-task']
  if len(heads)!=1 or len(tasks)!=1:raise ValueError('task ownership')
  head,task=heads[0],tasks[0]
  code=0xd0173 if scene in(0,5)else 0xd0174
  if task['unit']!=head['state']['unit']or task['target']!=head['order']['target']or task['code']!=code or task['range']!=(0x43960000 if scene in(0,5)else 0)or task['caller']!=('5fd7b1'if scene in(0,5)else'5fd81c'):raise ValueError('structure task/range differs')
  state=task['targetState'];has_move=state['move']!='0x0'
  if has_move!=(scene not in(1,2,3))or bool(state['flags']&0x08000000)!=(scene==4):raise ValueError('Move ability/root classification differs')
  settled=[r['value']for r in selected if r['event']=='marker'and 'label=settled'in r['value']]
  if len(settled)!=1 or int(re.search(r'order=(\d+)',settled[0])[1])!=(command if scene in(0,5)else 0):raise ValueError('public completion differs')
  result.append(dict(scene=scene,code=task['code'],range=task['range'],caller=task['caller'],flags=state['flags'],flags248=state['flags248'],has_move=has_move))
 return result

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('repeats/control required')
 repeated=[]
 for c in captures:
  rows=c['rows'];meta,footer=rows[0],rows[-1];public=markers(c['preload'])
  if meta['task']!='STRUCTURE-FOLLOW'or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=len(public)or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows):raise ValueError('capture provenance/completion')
  for name in('follow278_capture.py','follow278_observer.js','follow278_make_map.py','follow278_probe.j'):
   if meta['source_sha256'][name]!=spec['source_sha256']['tools/frida/research/'+name]:raise ValueError('captured source differs')
  if public!=spec['public']or len(public)!=797:raise ValueError('original public movement differs')
  if meta['mode']=='control':
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('hooked control')
  else:
   ends=[r for r in rows if r['event']=='trace-end']
   if len(ends)!=1 or ends[0].get('readOnly')is not True or dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts']:raise ValueError('observer incomplete')
   repeated.append(decisions(rows))
 if repeated[0]!=repeated[1]or repeated[0]!=spec['decisions']:raise ValueError('repeated native decisions differ')
 return dict(captures=2,controls=1,public_markers=797,task_cases=6)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text())
 if spec['game_sha256']!=SHA or spec['engine_patterns']!=TESTS:raise ValueError('contract changed')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source pin '+name)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('damaged frozen captures')
 evidence=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec);original_bytes(a.binary,spec['instructions'])
 engine=[]
 for tft in(False,True):
  for i,pattern in enumerate(TESTS):
   stem=a.report.with_name(a.report.stem+('-tft'if tft else'-classic')+f'-{i}');xml=stem.with_suffix('.xml');log=stem.with_suffix('.log')
   if xml.exists()or log.exists():raise ValueError('fresh engine outputs required')
   with log.open('w')as out:r=subprocess.run([str(a.test_binary.resolve()),'-data',str(a.data.resolve()),*(['-tft']if tft else[]),'+dedicated','1','+test',pattern],stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml.resolve())},timeout=120)
   x=ET.parse(xml).getroot();cases=list(x.iter('testcase'))
   if r.returncode or not cases or any(c.findall('failure')for c in cases):raise ValueError('engine '+str(log))
   engine.append(dict(tft=tft,pattern=pattern,tests=len(cases),assertions=int(x.get('assertions'))))
 report=dict(status='verified',passed=True,binary_sha256=SHA,instructions=len(spec['instructions']),**evidence,engine=engine,scope=spec['scope']);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
