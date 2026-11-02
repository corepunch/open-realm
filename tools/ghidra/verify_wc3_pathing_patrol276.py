#!/usr/bin/env python3
"""Recheck frozen original Patrol captures, instruction bodies and actual engine journeys.

This validates archived live motion; it does not claim full numerical crowd parity.
"""
import argparse,hashlib,json,os,re,struct,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_patrol_motion_trace import decode,read_rows,verify_contract,render_header
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SPEC=ROOT/'tools/ghidra/fixtures/retail-patrol276-1.27.json'
MOTION=ROOT/'tools/ghidra/fixtures/retail-patrol-motion-1.27.json'
HEADER=ROOT/'games/warcraft-3/game/tests/retail_patrol_motion.h'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(p):return re.findall(r'call Preload\( "(P276 [^"\r\n]*)" \)',p.read_text())
def verify_evidence(spec,archive):
 if spec['game_sha256']!=SHA:raise ValueError('wrong original contract')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source pin '+name)
 for name,h in spec['archive_sha256'].items():
  if digest(archive/name)!=h:raise ValueError('archive pin '+name)
 fixture=json.loads(MOTION.read_text());verify_contract(fixture)
 if HEADER.read_text()!=render_header(fixture):raise ValueError('literal engine header')
 for name in ('motion-a.jsonl','motion-b.jsonl'):
  rows=read_rows(archive/name)
  if rows[0]['source_sha256']['map']!=spec['motion_map_sha256']or rows[0]['source_sha256']['wc3_patrol_motion_probe.j']!=spec['motion_probe_sha256']:raise ValueError('motion provenance')
  if decode(rows)!=fixture['samples']:raise ValueError('frozen original motion')
 public=None
 for i in (1,2,3):
  rows=read_rows(archive/f'patrol276-live{i}.jsonl');meta=rows[0]
  if meta['sha256']!=SHA or meta['source_sha256']['map']!=spec['group_map_sha256']or meta['mode']!=('control'if i==3 else'observe'):raise ValueError('group provenance')
  end=rows[-1]
  if end.get('event')!='preload-file'or not end['complete']or end['markers']!=405:raise ValueError('incomplete group capture')
  current=markers(archive/f'patrol276-live{i}-preload.txt')
  if len(current)!=405 or not current[-1].startswith('P276 tick=100 label=complete'):raise ValueError('public group completion')
  if public is not None and current!=public:raise ValueError('observer/control public difference')
  public=current
  points=[r for r in rows if r.get('event')=='patrol-point']
  if i<3:
   if [r['value']for r in rows if r.get('event')=='marker']!=current:raise ValueError('native/public marker difference')
   if len(points)!=11 or len({r['unit']for r in points})!=3:raise ValueError('singleton coverage')
   if any(r['code']!=0xd016b or r['range'] or r['association']!='0x0'for r in points):raise ValueError('Patrol point task contract')
  elif points or any(r.get('event')in('marker','module','patrol-leg')for r in rows):raise ValueError('hooked control')
 sequence=None
 for i in (1,2):
  rows=read_rows(archive/f'patrol276-groups{i}.jsonl')
  if rows[0]['sha256']!=SHA or rows[0]['mode']!='observe' or rows[0]['source_sha256']['map']!=spec['group_map_sha256']:raise ValueError('physical group provenance')
  if not rows[-1].get('complete')or markers(archive/f'patrol276-groups{i}-preload.txt')!=public:raise ValueError('physical group/control motion')
  binds=[r for r in rows if r.get('event')=='physical-bind'];movers={};normalized=[]
  for b in binds:
   movers.setdefault(b['mover'],len(movers));normalized.append([movers[b['mover']],b['identity'],b['count']])
  if len(binds)!=12 or len(movers)!=4 or any(b['count']!=1 for b in binds)or len({tuple(b['identity'])for b in binds[:4]})!=4:raise ValueError('physical singleton membership')
  if sequence is not None and normalized!=sequence:raise ValueError('physical binding repeat')
  sequence=normalized
 return dict(motion_captures=2,public_motion_samples=3000,group_captures=4,group_controls=1,physical_bind_captures=2,physical_binds_per_repeat=12,public_group_markers=405,point_tasks_per_repeat=11)
def verify_instructions(binary,spec):
 data=binary.read_bytes()
 if hashlib.sha256(data).hexdigest()!=SHA:raise ValueError('unsupported original binary')
 pe=struct.unpack_from('<I',data,0x3c)[0];count=struct.unpack_from('<H',data,pe+6)[0];size=struct.unpack_from('<H',data,pe+20)[0];base=struct.unpack_from('<I',data,pe+24+28)[0];sections=[]
 for i in range(count):
  offset=pe+24+size+40*i;virtual_size,va,raw_size,raw=struct.unpack_from('<IIII',data,offset+8);sections.append((va,max(virtual_size,raw_size),raw))
 for address,code in spec['instructions'].items():
  rva=int(address,16)-base
  found=next((raw+rva-va for va,length,raw in sections if va<=rva<va+length),None)
  if found is None or data[found:found+len(code)//2].hex()!=code:raise ValueError('instruction '+address)
 return len(spec['instructions'])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=verify_evidence(spec,a.archive);instructions=verify_instructions(a.binary,spec);engine=[]
 for tft in(False,True):
  for index,pattern in enumerate(spec['engine_patterns']):
   stem=a.report.with_name(a.report.stem+('-tft'if tft else'-classic')+f'-{index}');xml=stem.with_suffix('.xml');log=stem.with_suffix('.log')
   if xml.exists()or log.exists():raise ValueError('fresh engine outputs required')
   with log.open('w')as out:r=subprocess.run([str(a.test_binary.resolve()),'-data',str(a.data.resolve()),*(['-tft']if tft else[]),'+dedicated','1','+test',pattern],stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml.resolve())},timeout=120)
   x=ET.parse(xml).getroot();cases=list(x.iter('testcase'))
   if r.returncode or not cases or any(c.findall('failure')for c in cases):raise ValueError('engine '+str(log))
   engine.append(dict(tft=tft,pattern=pattern,tests=len(cases),assertions=int(x.get('assertions')),returncode=r.returncode))
 report=dict(status='verified',passed=True,binary_sha256=SHA,instructions=instructions,**evidence,engine=engine,scope=spec['scope']);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
