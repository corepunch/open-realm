#!/usr/bin/env python3
"""Verify public constructor/placement policies against exact retail captures."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-place309-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.place309*','wc3_movement.place308*','wc3_movement.goal308*','wc3_movement.foot307*','wc3_movement.stop247*','wc3_movement.stop258*','wc3_movement.target*','wc3_movement.follow*','wc3_movement.smart*','wc3_movement.retail_gate_blocked_and_outside*','wc3_movement.public_move_matches_original*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions','pathfinding.indexed_placement_matches_live_scalar_occupancy_and_publication']
SOURCES=['tools/frida/research/place309_'+s for s in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Place309Evidence.java','games/warcraft-3/game/skills/fixtures/retail_place309.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(P309 [^"\r\n]*)" \)',text)
def normalize(value):
 if isinstance(value,list):return [normalize(x)for x in value]
 if isinstance(value,dict):return {k:normalize(v)for k,v in value.items()}
 if isinstance(value,str)and re.fullmatch('0x[0-9a-f]+',value):return value!='0x0'
 return value

def stages(rows):
 out=[]
 for scene in range(72):
  rr=[x for x in rows if x.get('scene')==scene];prior=-1;states=[]
  for label in('created','placed','queryzero'):
   mark=[x for x in rr if x['event']=='marker'and 'label='+label+' 'in x['value']]
   if len(mark)!=1:raise ValueError('missing stage marker')
   block=[x for x in rr if prior<x['seq']<mark[0]['seq']];prior=mark[0]['seq']
   delta=[x for x in block if x['event']=='delta-leave'];admit=[x for x in block if x['event']=='admit-leave'];writes=[x for x in block if x['event']=='position-leave'];policies=[x for x in block if x['event']=='placement-enter']
   if len(delta)!=(2 if label=='created'else 1)or len(admit)!=1 or len(writes)!=1 or len(policies)!=1:raise ValueError('missing native stage')
   m=delta[-1]['m'];policy={k:policies[0][k]for k in('radius','mask','lane','filter','budget','limit','scan','integer')}
   if policy['radius']!=m['radius']or policy['limit']!=32 or policy['scan']!=2 or policy['integer']!=(1 if 24<=scene<48 else 0)or policy['mask']!=(0 if label=='queryzero'else [0x02000002,0x04000004,0x80000080][scene//24])or policy['filter']!=0xffffffff or policy['budget']!=0 or policy['lane']!=0:raise ValueError('placement policy differs')
   if admit[0]['result']!=1:raise ValueError('admission result differs')
   states.append(dict(fine=m['pos'],world=writes[0]['point'],radius=m['radius'],result=admit[0]['result'],policy=policy))
  out.append(states)
 return out

def validate(spec,bundle=None):
 if spec['version']!=1 or spec['task']!='FOOT-04.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or len(spec['instructions'])!=spec['instruction_count']or spec['instruction_count']<900 or set(spec['pins'])!=set(SOURCES):raise ValueError('placement contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen captures changed')
 caps=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('two repeats and unhooked control required')
 streams=[]
 for cap in caps:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'])
  if meta['task']!='FOOT-04.2/place309'or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or footer['event']!='preload-file'or not footer.get('complete')or public!=spec['public']or len(public)!=361 or footer['markers']!=361 or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256']or any(x.get('type')=='error'or x.get('event')=='trace-failed'for x in rows):raise ValueError('capture provenance or public control')
  for p in SOURCES[:7]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  live=[x for x in rows if 'seq'in x and x['event']!='trace-end']
  if meta['mode']=='control':
   if live or any(x['event']=='trace-end'for x in rows):raise ValueError('hooked control')
   continue
  ends=[x for x in rows if x['event']=='trace-end']
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or len(live)!=3962 or [x['seq']for x in live]!=list(range(len(live)))or dict(Counter(x['event']for x in live))!=ends[0]['counts']:raise ValueError('lost native observer events')
  if [x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=public:raise ValueError('live/preload marker mismatch')
  if stages(rows)!=spec['states']:raise ValueError('frozen placement state differs')
  streams.append(normalize(live))
 if streams[0]!=streams[1]:raise ValueError('repeated native streams differ')
 header=(ROOT/SOURCES[-1]).read_text();words=[int(x,16)for x in re.findall(r'0x([0-9a-f]{8})u',header)]
 expected=[x for row in spec['states']for st in row for x in st['fine']+st['world']+[st['radius'],st['result']]]
 if words!=expected:raise ValueError('literal engine words differ from retail')
 return dict(captures=2,controls=1,scenes=72,states=216,public_markers=361,native_records=3962)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
