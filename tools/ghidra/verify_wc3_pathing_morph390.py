#!/usr/bin/env python3
"""Verify concrete StoneForm configured availability, takeoff-rebind inverse and detach."""
import argparse,base64,collections,gzip,hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path
from verify_wc3_pathing_morph380 import ROOT,SHA,EVENTS as PARENT_EVENTS,WRAPPER_EVENTS,TESTS as PARENT_TESTS,PINS as PARENT_PINS,guards as parent_guard_inventory
from wc3_morph374_lifetimes import project,CONTRACT
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-morph390-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-morph390-inputs-1.27.json.gz'
EVENTS=PARENT_EVENTS|{n+s for n in ['stone-forward-begin','stone-forward-callback','stone-reverse-begin','stone-detach','morph-detach',
    'stone-clock-cancel','counter15c-acquire','counter15c-release']for s in('-enter','-leave')}|{'stone-periodic-timer'}
TESTS=['wc3_unit.morph390*']+PARENT_TESTS
PINS=['tools/frida/research/'+n for n in ['payoff390_make_map.py','payoff390_probe.j','payoff390_observer.js','payoff390d_make_map.py','payoff390d_probe.j']]+['tools/ghidra/'+n for n in ['verify_wc3_pathing_morph390.py','research/morph390_freeze.py','research/morph390_static_export.py','fixtures/retail-morph390-ghidra-1.27.json']]
PINS=sorted(set(PINS)|set(PARENT_PINS))
MARK=re.compile(r'P390(D?) setting=([01]) invisible=([01]) tick=(\d+) scene=(\d+) label=([a-z-]+) accepted=([01]) type=(\d+) morph=(\d+) passive=(\d+) order=(\d+)')
UGAR,UGRM=0x75676172,0x7567726d
LANDING,TAKEOFF=1063256064,1052770304 # .875 = Dur.375+DataC.5; .375
sha=lambda raw:hashlib.sha256(raw).hexdigest()
def guards():
 instructions,data=parent_guard_inventory()
 proof=json.loads((ROOT/'tools/ghidra/fixtures/retail-morph390-ghidra-1.27.json').read_text())
 for destination,source in [(instructions,proof['instructions']),(data,proof['data_guards'])]:
  for address,raw in source.items():
   if address in destination and destination[address]!=raw:raise ValueError('conflicting native guard')
   destination[address]=raw
 # +a4 6f500230, +404 54cb50, v41c 54c640, +414 50a540 and the literal RTTI name.
 if len(instructions)!=4179 or len(data)!=29 or data.get('6fafebe0')!='3002506f' or data.get('6fafef40')!='50cb546f' or data.get('6fafef58')!='40c6546f' or data.get('6fafef50')!='40a5506f' or data.get('6fd1b3a4')!=b'.?AVCAbilityStoneForm@@\0'.hex():raise ValueError('missing concrete StoneForm routing proof')
 return instructions,data

def decode(case,key):return base64.b64decode(case[key],validate=True)
def parse_rows(case):return [json.loads(x)for x in decode(case,'capture').decode().splitlines()]
def public(case,prefix):return re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',decode(case,'preload').decode())
def fields(value):
 match=MARK.fullmatch(value)
 if not match:raise ValueError('unrecognized public marker')
 return dict(zip(['detach','setting','invisible','tick','scene','label','accepted','type','morph','passive','order'],[bool(match[1])]+[int(match[i])if i!=6 else match[i]for i in range(2,12)]))

def takeoff_release_precedes_separation(rows,ticks):
 # 562c50 reverse branch: rebind to base, 54ff50 ->+414 ->+404 release, then 569e20 acquires Unit198.
 for start in [i for i,r in enumerate(rows)if r['event']=='stone-reverse-begin-enter']:
  names=[r['event']for r in rows[start:]]
  if 'morph-form-enable-leave'not in names or 'morph-begin-enter'not in names:raise ValueError('missing takeoff release/separation')
  enable=start+names.index('morph-form-enable-leave');begin=start+names.index('morph-begin-enter')
  if not enable<begin or rows[enable]['tick']!=rows[start]['tick']or rows[enable]['state']['rawcode']!=UGAR or rows[start]['state']['rawcode']!=UGAR:raise ValueError('takeoff release does not follow base rebind before separation')
 if [r['tick']for r in rows if r['event']=='stone-reverse-begin-enter']!=ticks:raise ValueError('wrong reverse rebind frontier')

def semantic(rows,markers,setting,detach):
 samples=[fields(x)for x in markers if ' label=start'not in x]
 if any(x['setting']!=setting or x['detach']!=detach for x in samples):raise ValueError('wrong public setting/producer')
 if [r['result']for r in rows if r['event']=='morph-policy-leave']!=[setting]*(8 if detach else 21):raise ValueError('wrong concrete policy return')
 if [r['delay']for r in rows if r['event']=='transition-timer']!=([LANDING]*2 if detach else [LANDING,TAKEOFF,LANDING,TAKEOFF,TAKEOFF]):raise ValueError('wrong landing/takeoff transition')
 positive=[x for x in samples if x['label']=='pre-forward-visibility']
 if len(positive)!=(2 if detach else 1)or any(x['invisible']!=1 for x in positive):raise ValueError('missing positive enemy visibility witness')
 for x in samples:
  if x['label']in ('forward','reverse','detach-morph')and x['accepted']!=1:raise ValueError('public producer rejected')
 acquired=[x for x in rows if x['event']=='morph-form-disable-leave'];released=[x for x in rows if x['event']=='morph-form-enable-leave']
 if any(x['availability']['class']!='afeb3c'for x in acquired+released):raise ValueError('wrong concrete availability class')
 if detach:
  if [x['tick']for x in acquired]!=[12,90]or [x['tick']for x in released]!=[12,94]:raise ValueError('wrong detach acquisition/release frontier')
  takeoff_release_precedes_separation(rows,[12,94])
  leaves=[x for x in rows if x['event']=='stone-detach-leave' and x['tick']in(12,94)]
  if len(leaves)!=2 or any(x['state']['rawcode']!=UGAR or x['state']['disableDepth']!=0 or x['state']['invisibilityDepth']!=1 for x in leaves):raise ValueError('wrong concrete detach inverse')
  for x in leaves:
   if bool(x['state']['flags']&0x1000000)!=(not setting)or bool(any(x['state']['invisibilityScalar'][4:]))!=bool(setting):raise ValueError('wrong detach fade state')
  detached=[x for x in samples if x['label']=='detach-morph']
  if len(detached)!=2 or any(x['type']!=UGAR or x['morph']!=0 or x['passive']!=1 or x['order']!=0 or x['invisible']!=1-setting for x in detached):raise ValueError('wrong public detach frontier')
  later=[x for x in samples if x['label']in ('after-detach-fade','later-detach-visibility')]
  if len(later)!=4 or any(x['invisible']!=1 for x in later):raise ValueError('missing later detach fade completion')
 else:
  if [x['tick']for x in acquired]!=[20,90,140]or any(x['state']['rawcode']!=UGRM for x in acquired):raise ValueError('wrong landing-rebind acquisition frontier')
  if [x['tick']for x in released]!=[34,104,174]:raise ValueError('wrong takeoff-rebind release frontier')
  takeoff_release_precedes_separation(rows,[34,104,174])
  for scene in range(3):
   ages=[x for x in samples if x['label']=='sample' and x['scene']==scene and 35<=x['tick']-scene*70<=47]
   expected=[1]*13 if setting==0 else [0]*7+[1]*6
   if [x['invisible']for x in ages]!=expected:raise ValueError('missing takeoff-release fade witness')

def check_group(cases,expected,setting,detach,pins,controllers):
 if set(cases)!={'observe_a','observe_b','control_a'}:raise ValueError('missing repeat/control')
 prefix='P390D'if detach else'P390';count=152 if detach else 221
 projections=[];reference=None
 for mode,case in cases.items():
  if sha(decode(case,'capture'))!=expected['capture_sha256'][mode]or sha(decode(case,'preload'))!=expected['preload_sha256'][mode]:raise ValueError('original capture/preload bytes differ')
  rows=parse_rows(case);metadata=rows[0];markers=public(case,prefix)
  if any(r['event']not in EVENTS|WRAPPER_EVENTS for r in rows):raise ValueError('unexpected event; no silent drops')
  if len(markers)!=count or rows[-1].get('event')!='preload-file'or rows[-1].get('complete')is not True or rows[-1]['markers']!=count or rows[-1]['sha256']!=sha(decode(case,'preload')):raise ValueError('incomplete public footer')
  if metadata.get('sha256')!=SHA or not metadata.get('owned')or metadata.get('env')!=expected['environments'][mode]or metadata.get('prefix')!=prefix+' 'or metadata['source_sha256']['map']!=expected['map_sha256']:raise ValueError('capture provenance differs')
  for name in ['separation366_loaded_capture.py','payoff390_observer.js','group338_make_map.py','group338_probe.j','queue263_ui_input.c','order0110_ui_input.c']:
   if metadata['source_sha256'].get(name)!=pins['tools/frida/research/'+name]:raise ValueError('capture source differs '+name)
  for name,raw in controllers.items():
   if metadata['source_sha256'].get(name)!=sha(base64.b64decode(raw,validate=True)):raise ValueError('controller pin differs')
  if reference is None:reference=markers
  if markers!=reference:raise ValueError('literal public control/repeat differs')
  if mode.startswith('observe'):
   if metadata['mode']!='observe'or [r['value']for r in rows if r['event']=='marker']!=markers:raise ValueError('native/public stream differs')
   ends=[r for r in rows if r['event']=='trace-end']
   if len(ends)!=1 or ends[0].get('readOnly')is not True or ends[0]['counts']!=dict(collections.Counter(r['event']for r in rows if 'seq'in r)):raise ValueError('native completeness counts differ')
   first=next(i for i,r in enumerate(rows)if r['event']=='marker'and ' label=start'in r['value'])
   module=next(r for r in rows if r['event']=='module');start=int(module['base'],16);end=start+metadata['imageSize']
   exceptions=[(i,r)for i,r in enumerate(rows)if r['event']=='native-exception']
   if len(exceptions)!=2 or any(i>=first or r['type']!='system'or start<=int(r['address'],16)<end for i,r in exceptions):raise ValueError('reported startup exception chronology differs')
   semantic(rows,markers,setting,detach)
   projection,ledger=project(rows)
   if ledger!=case['retirement_ledger']:raise ValueError('literal lifetime ledger differs')
   if projection!=expected['projection']:raise ValueError('complete scalar/public/exception projection differs')
   projections.append(projection)
  elif metadata['mode']!='control':raise ValueError('wrong control mode')
 if projections[0]!=projections[1]:raise ValueError('full projected repeats differ')
 return dict(captures=3,scenes=2 if detach else 3,public_markers=count,projected_events=len(projections[0]),native_events=sum(x['event']not in('marker','native-exception')for x in projections[0]),recorded_system_exceptions=sum(x['event']=='native-exception'for x in projections[0]))

def runtime(bundle,spec):
 if set(bundle['groups'])!={'ordinary0','ordinary1','detach0','detach1'}or set(spec['groups'])!=set(bundle['groups']):raise ValueError('missing concrete settings/scenes')
 totals=collections.Counter()
 for key,cases in bundle['groups'].items():totals.update(check_group(cases,spec['groups'][key],int(key[-1]),key.startswith('detach'),spec['pins'],bundle['controllers']))
 return dict(totals)
def validate(spec):
 instructions,data=guards()
 if spec.get('task')!='SEP-01.2'or spec.get('version')!=1 or spec.get('game_sha256')!=SHA or spec.get('projection_contract')!=CONTRACT or spec.get('engine_tests')!=TESTS or spec.get('instructions')!=instructions or spec.get('data_guards')!=data or set(spec.get('pins',{}))!=set(PINS):raise ValueError('changed complete producer contract')
 for path,pin in spec['pins'].items():
  if not matches_capture_source(ROOT,path,pin):raise ValueError('changed source '+path)
 if sha(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed original raw bundle')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle,spec)
 if {name:sha(base64.b64decode(raw,validate=True))for name,raw in bundle['diagnostics'].items()}!=spec['diagnostics']:raise ValueError('changed retained launch diagnostic')
 if set(bundle['controllers'])!={'queue263-ui.exe','queue263-ui.exe.so'}or sha(base64.b64decode(bundle['map_input'],validate=True))!=spec['base_sha256']:raise ValueError('missing controller/base proof')
 return bundle

def rebuild(bundle,spec):
 with tempfile.TemporaryDirectory(prefix='morph390-map-')as directory:
  root=Path(directory);base=root/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input'],validate=True))
  for key,manifest in bundle['manifests'].items():
   builder='payoff390d_make_map.py'if key.startswith('detach')else'payoff390_make_map.py';out=root/(key+'.w3m')
   subprocess.run([sys.executable,str(ROOT/'tools/frida/research'/builder),'--base',str(base),'--output',str(out),'--mpqtool',str(ROOT/'build/bin/mpqtool'),'--alternate-disable',key[-1]],check=True,stdout=subprocess.DEVNULL)
   if sha(out.read_bytes())!=spec['groups'][key]['map_sha256']or json.loads(out.with_suffix('.json').read_text())!=manifest:raise ValueError('deterministic map reconstruction differs')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--native-only',action='store_true');p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions']|spec['data_guards']);rebuild(bundle,spec)
 result=dict(passed=True,status='native-verified'if a.native_only else'verified',binary_sha256=SHA,instructions=4179,data_ranges=29,**runtime(bundle,spec))
 if not a.native_only:
  import verify_wc3_pathing_work242 as runner
  old=runner.TESTS
  try:runner.TESTS=TESTS;result['engine']=runner.run_engine(a.test_binary,a.data,a.report)
  finally:runner.TESTS=old
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
