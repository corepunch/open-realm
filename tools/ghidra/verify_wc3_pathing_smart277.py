#!/usr/bin/env python3
"""Verify original Shift RMB activation and the engine's retained-point ownership."""
import argparse,gzip,hashlib,json,os,re,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-smart277-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS=['wc3_movement.smart277*','wc3_api.current_order_follow_target_removal_is_synchronous','wc3_movement.target220*','wc3_order_lifecycle.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(S277 [^"\r\n]*)" \)',text)
def window(values):return[v for v in values if int(re.search(r'tick=(\d+)',v)[1])%240>=80]
def decisions(rows,scenes):
 result=[]
 for scene in scenes:
  selected=[r for r in rows if r.get('scene')==scene]
  appends=[r for r in selected if r['event']=='append-enter' and r['order']['command']==851971]
  heads=[r for r in selected if r['event']=='dispatch-head' and r['order']['command']==851971]
  if len(appends)!=1 or len(heads)!=1:raise ValueError('queued Smart ownership')
  packet=appends[0]['order'];head=heads[0];unit=head['state']['unit']
  if packet['flags']!=4 or appends[0]['state']['count']!=1 or head['order']['identity']!=packet['identity'] or head['order']['point']!=packet['point'] or head['state']['count']!=1:raise ValueError('retained packet')
  end=next(r['seq']for r in selected if r.get('seq',0)>head['seq']and r['event']=='marker')
  tasks=[r for r in selected if head['seq']<r.get('seq',0)<end and r['event']in('point-task','target-task')]
  if scene!=6:
   if len(tasks)!=1 or tasks[0]['event']!='point-task' or tasks[0]['code']!=0xd016b or tasks[0]['point']!=packet['point'] or tasks[0]['unit']!=unit or tasks[0]['caller']!='5fd95d':raise ValueError('lost optional target must retain point')
  else:
   if [r['code']for r in tasks]!=[0xd0193,0xd0173,0xd0193,0xd0174]or any(r['unit']!=unit for r in tasks)or[t['target']for t in tasks]!=[None,packet['target'],packet['target'],packet['target']]:raise ValueError('visible Smart target chain')
  preceding=[r['value']for r in selected if r['event']=='marker' and r['seq']<head['seq']]
  tick=int(re.search(r'tick=(\d+)',preceding[-1])[1])
  if tick%240<=80:raise ValueError('target must change before activation')
  result.append(dict(scene=scene,point=packet['point'],activation_tick=tick,tasks=[{k:r[k]for k in('event','code','point','caller')if k in r}for r in tasks]))
 return result

def validate_runtime(bundle,spec):
 total=0
 for name,scenes in [('remove',[2]),('matrix',list(range(1,7)))]:
  captures=bundle[name]
  if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('repeats/control required')
  repeated=[]
  for c in captures:
   rows=c['rows'];meta=rows[0];footer=rows[-1];public=markers(c['preload'])
   if meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['maps'][name]or not footer.get('complete')or footer['markers']!=len(public)or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows):raise ValueError('capture provenance/completion')
   if window(public)!=spec['public'][name]:raise ValueError('original public movement differs')
   inputs=[r for r in rows if r['event']=='player-input']
   if len(inputs)!=len(scenes)or any(r['rc']or not r['plan']['shift']or r['plan']['key']!='smart'for r in inputs):raise ValueError('genuine Shift RMB required')
   if meta['mode']=='control':
    if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('hooked control')
   else:
    ends=[r for r in rows if r['event']=='trace-end']
    if len(ends)!=1 or ends[0].get('readOnly')is not True:raise ValueError('observer incomplete')
    repeated.append(decisions(rows,scenes))
  if repeated[0]!=repeated[1]or repeated[0]!=spec['decisions'][name]:raise ValueError('repeated activation differs')
  total+=len(spec['public'][name])
 return dict(captures=4,controls=2,public_markers=total,loss_cases=5,visible_cases=1)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text())
 if spec['game_sha256']!=SHA or spec['engine_patterns']!=TESTS:raise ValueError('contract changed')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source pin '+name)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('damaged frozen captures')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));evidence=validate_runtime(bundle,spec)
 original_bytes(a.binary,spec['instructions'])
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
