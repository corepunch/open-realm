#!/usr/bin/env python3
"""Verify retained bridge placement levels, replacement footprints and retail admission."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-bridge311-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.bridge311*','wc3_support.*','wc3_model.*','wc3_destructable.*','wc3_fine_spatial.*','wc3_map_lifetime.*','wc3_movement.place309*','wc3_movement.goal310*','wc3_movement.goal308*','wc3_movement.place308*','wc3_movement.foot307*','wc3_movement.stop247*','wc3_movement.stop258*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/bridge311_'+x for x in('capture.py','observer.js','make_map.py','probe.j','diagnostic.js','lifetime.js')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Bridge311Evidence.java','games/warcraft-3/game/skills/fixtures/retail_bridge311.h','games/warcraft-3/game/tests/fixtures/retail_bridge311_grids.h','games/warcraft-3/tests/resources-src/PathTextures/CityBridgeLarge0Death.tga','games/warcraft-3/game/tests/fixtures/retail_walkmesh229.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(B311 [^"\r\n]*)" \)',text)
def normalize(v):
 if isinstance(v,list):return [normalize(x)for x in v]
 if isinstance(v,dict):
  out={k:normalize(x)for k,x in v.items()}
  if out.get('event')=='deck-registration':
   for e in out['entries']:
    for i in (0,9):e['header'][i]=bool(e['header'][i])
  # Non-unit mover overlays do not have a group identity at this offset.
  # Preserve all observed words, normalizing just its process pointer.
  if out.get('event')in('delta-enter','delta-leave')and out['m']['maximum']==0x7f7fffff and out['m']['radius']==0 and out['m']['range']==0:
   out['m']['group']=[bool(x)for x in out['m']['group']]
  return out
 if isinstance(v,str)and re.fullmatch('0x[0-9a-f]+',v):return v!='0x0'
 return v

def states(rows):
 out=[]
 for scene in range(178):
  rr=[x for x in rows if x.get('scene')==scene];prior=-1;stages=[]
  if sum(x['event']=='marker'and'label=end-cleanup'in x['value']for x in rr)!=1:raise ValueError('incomplete scene')
  for label in ('created','placed','queryzero'):
   m=next(x for x in rr if x['event']=='marker'and'label='+label+' 'in x['value'])
   block=[x for x in rr if prior<x.get('seq',-1)<m['seq']]
   mover=[x for x in block if x['event']=='delta-leave'][-1]['m']
   world=[x for x in block if x['event']=='position-leave'][-1]['point']
   admit=[x for x in block if x['event']=='admit-leave'][-1]
   stages.append(dict(fine=mover['pos'],world=world,radius=mover['radius'],result=admit['result']))
   prior=m['seq']
  out.append(stages)
 return out

def render_header(expected,wpm):
 s='/* Literal public retail311 captures: constructor, placement, queryzero. */\nstatic uint32_t const bridge311_states[178][3][6]={\n'
 for stage in expected:s+='    {'+','.join('{'+','.join('0x%08xu'%x for x in st['fine']+st['world']+[st['radius'],st['result']])+'}'for st in stage)+'},\n'
 s+='};\nstatic uint8_t const bridge311_wpm[4096]={\n'
 for i in range(0,len(wpm),64):s+='    '+','.join('0x%02x'%x for x in wpm[i:i+64])+',\n'
 return s+'};\n'

def render_grids(grids):
 s='/* All four complete presentation grids, identical read-only retail repeats. */\n#pragma once\n'
 for i,g in enumerate(grids):s+=f'static uint32_t const bridge311_grid{i}[]={{'+','.join('0x%08xu'%x for x in g['values'])+'};\n'
 s+='static struct {uint32_t shape[4];uint32_t const *values;} const bridge311_grids[]={\n'
 for i,g in enumerate(grids):s+=' {{'+','.join('0x%08xu'%x for x in g['shape'])+'},bridge311_grid'+str(i)+'},\n'
 return s+'};\n'

def check_cap(spec,cap,observer,count,levels=(4,6,9)):
 rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'])
 if meta['task']!='FOOT-04.2/bridge311'or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['display']!={'B':':98','C':':99'}[meta['env']]or meta['remote']!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']]or meta['source_sha256']['map']!=spec['map_sha256']or footer['event']!='preload-file'or not footer.get('complete')or public!=spec['public']or len(public)!=909 or footer['markers']!=909 or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256']or any(x.get('type')=='error'or x.get('event')=='trace-failed'for x in rows):raise ValueError('capture provenance or public control')
 captured=[SOURCES[i]for i in (0,2,3,6,7,8)]+[observer]
 for p in captured:
  if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
 live=[x for x in rows if 'seq'in x and x['event']!='trace-end']
 if meta['mode']=='control':
  if live or any(x['event']=='trace-end'for x in rows):raise ValueError('hooked control')
  return []
 ends=[x for x in rows if x['event']=='trace-end']
 if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or len(live)!=count or [x['seq']for x in live]!=list(range(len(live)))or dict(Counter(x['event']for x in live))!=ends[0]['counts']:raise ValueError('lost native events')
 if [x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=public:raise ValueError('live/preload markers differ')
 if states(rows)!=spec['states']:raise ValueError('frozen placement differs')
 reg=[x for x in live if x['event']=='deck-registration'];delta=[x for x in live if x['event']=='deck-level-delta']
 if len(reg)!=3 or [x['result']for x in delta]!=[n-2 for n in levels]or len({x['object']for x in reg})!=3:raise ValueError('provider delta or identity differs')
 for i,row in enumerate(reg):
  if row['enabled']!=1 or len(row['entries'])!=i+1 or row['object']!=row['entries'][-1]['object']:raise ValueError('registration lifetime differs')
  for j,e in enumerate(row['entries']):
   if e['object']!=reg[j]['object']or e['header'][1:9]!=[2,levels[j]-2,12,16,29,47,576,576]or e['header'][10]!=64 or e['levels']!=[levels[j]]*576:raise ValueError('level rectangle differs')
 return normalize(live)

def validate(spec,bundle=None):
 if spec['version']!=1 or spec['task']!='FOOT-04.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or len(spec['instructions'])!=spec['instruction_count']or spec['instruction_count']!=1080 or set(spec['pins'])!=set(SOURCES):raise ValueError('bridge contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen captures changed')
 data=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
 caps=data['base']
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('repeats and control required')
 base=[check_cap(spec,c,SOURCES[1],27911)for c in caps]
 if base[0]!=base[1]:raise ValueError('repeated native stream differs')
 for key,observer,count in [('grids',SOURCES[4],27915),('lifetimes',SOURCES[5],27930)]:
  cc=data[key]
  if len(cc)!=2 or any(c['rows'][0]['mode']!='observe'for c in cc):raise ValueError('supplemental repeats required')
  profiles=spec['lifetime_levels']if key=='lifetimes'else[(4,6,9)]*2
  live=[check_cap(spec,c,observer,count,levels=profiles[i])for i,c in enumerate(cc)]
  if key=='grids'and live[0]!=live[1]:raise ValueError('supplemental stream differs '+key)
  if key=='grids':
   grids=[x for x in cc[0]['rows']if x['event']=='presentation-grid']
   if grids!=spec['grids']or len(grids)!=4 or sum(len(g['values'])for g in grids)!=340:raise ValueError('presentation grids differ')
  else:
   patch=[x for x in cc[0]['rows']if x['event']=='diagnostic-patch']
   if normalize(patch)!=spec['patch']:raise ValueError('retirement patch differs')
   other=[x for x in cc[1]['rows']if x['event']=='diagnostic-patch']
   if normalize(other)!=spec['patch']:raise ValueError('retirement repeat differs')
   # Supplemental reads expose a model-pose-dependent third level. Freeze both
   # observed variants; only the placement/retirement contract is common here.
   def footprint_projection(rows):
    return [x for x in rows if x['event']not in('deck-level-delta','deck-registration','effective-level')]
   if footprint_projection(live[0])!=footprint_projection(live[1]):raise ValueError('retirement/public-placement stream differs')
 if (ROOT/SOURCES[10]).read_text()!=render_header(spec['states'],spec['wpm']):raise ValueError('literal engine positions differ from retail')
 if (ROOT/SOURCES[11]).read_text()!=render_grids(spec['grids']):raise ValueError('literal engine grids differ from retail')
 return dict(captures=6,controls=1,scenes=178,placement_states=534,public_markers=909,native_records=27911,grid_values=340,provider_levels=[4,6,9],supplemental_provider_levels=spec['lifetime_levels'],model_pose_phase_open=True)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
