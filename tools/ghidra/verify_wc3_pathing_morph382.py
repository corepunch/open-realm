#!/usr/bin/env python3
"""Verify the complete frozen AEme/BEme policy, lifetime and inverse evidence."""
import argparse,ast,base64,collections,gzip,hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path
from verify_wc3_pathing_morph380 import ROOT,SHA,EVENTS as PARENT_EVENTS,WRAPPER_EVENTS,TESTS as PARENT_TESTS
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
from wc3_morph374_lifetimes import project,CONTRACT
import wc3_morph382_semantics as semantics
SPEC='tools/ghidra/fixtures/retail-morph382-1.27.json'
BUNDLE='tools/ghidra/fixtures/retail-morph382-inputs-1.27.json.gz'
PROOF='tools/ghidra/fixtures/retail-morph382-ghidra-1.27.json'
TESTS=['wc3_unit.morph382*','wc3_unit.metamorphosis382_pool_release_retires_primary_timer']+PARENT_TESTS
EVENTS=PARENT_EVENTS|{p+s for p in ['metamorphosis-forward-callback','metamorphosis-detach','metamorphosis-buff-attach','metamorphosis-buff-detach','metamorphosis-buff-removed']for s in ['-enter','-leave']}|{f'private-clock-{p}-{s}'for p in ['acquire','release','freeze','resume','schedule']for s in ['enter','leave']}
FAMILIES={'ordinary':('','Payoff382b-','P382',3,221),'detach':('detach-','Payoff382bd-','P382D',2,152),'public':('buff-remove-','Payoff382r-','P382R',3,224),'alias':('alias-','Payoff382a-','P382A',3,224),'pause':('pause-','Payoff382p-','P382P',4,384),'sleep':('nested-sleep-pause-','Payoff382w-','P382W',4,690)}
PINS=['tests/test_wc3_pathfinding_morph382.py', 'tools/frida/research/group338_make_map.py', 'tools/frida/research/group338_probe.j', 'tools/frida/research/morph330_make_map.py', 'tools/frida/research/morph374_make_map.py', 'tools/frida/research/morph374_observer.js', 'tools/frida/research/morph374_probe.j', 'tools/frida/research/morph374b_make_map.py', 'tools/frida/research/morph374b_probe.j', 'tools/frida/research/order0110_ui_input.c', 'tools/frida/research/payoff375_make_map.py', 'tools/frida/research/payoff375_observer.js', 'tools/frida/research/payoff375_probe.j', 'tools/frida/research/payoff375d_make_map.py', 'tools/frida/research/payoff375d_observer.js', 'tools/frida/research/payoff375d_probe.j', 'tools/frida/research/payoff380_make_map.py', 'tools/frida/research/payoff380_observer.js', 'tools/frida/research/payoff380_probe.j', 'tools/frida/research/payoff380b_observer.js', 'tools/frida/research/payoff380d_make_map.py', 'tools/frida/research/payoff380d_probe.j', 'tools/frida/research/payoff382_make_map.py', 'tools/frida/research/payoff382_observer.js', 'tools/frida/research/payoff382_probe.j', 'tools/frida/research/payoff382a_make_map.py', 'tools/frida/research/payoff382a_probe.j', 'tools/frida/research/payoff382d_make_map.py', 'tools/frida/research/payoff382d_probe.j', 'tools/frida/research/payoff382p_make_map.py', 'tools/frida/research/payoff382p_observer.js', 'tools/frida/research/payoff382p_probe.j', 'tools/frida/research/payoff382r_make_map.py', 'tools/frida/research/payoff382r_probe.j', 'tools/frida/research/payoff382s_make_map.py', 'tools/frida/research/payoff382s_observer.js', 'tools/frida/research/payoff382s_probe.j', 'tools/frida/research/payoff382t_make_map.py', 'tools/frida/research/payoff382t_observer.js', 'tools/frida/research/payoff382t_probe.j', 'tools/frida/research/payoff382u_make_map.py', 'tools/frida/research/payoff382u_observer.js', 'tools/frida/research/payoff382u_probe.j', 'tools/frida/research/payoff382v_make_map.py', 'tools/frida/research/payoff382v_observer.js', 'tools/frida/research/payoff382v_probe.j', 'tools/frida/research/payoff382w_make_map.py', 'tools/frida/research/payoff382w_observer.js', 'tools/frida/research/payoff382w_probe.j', 'tools/frida/research/queue263_ui_input.c', 'tools/frida/research/separation366_loaded_capture.py', 'tools/ghidra/fixtures/retail-burrow366-passives-1.27.json', 'tools/ghidra/fixtures/retail-group359-1.27.json', 'tools/ghidra/fixtures/retail-morph374-lifetime-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph374-rebind-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph374-removal-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph375-detach-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph375-policy-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph380-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-morph382-ghidra-1.27.json', 'tools/ghidra/fixtures/retail-submerge360-1.27.json', 'tools/ghidra/research/Morph374LifetimeEvidence.java', 'tools/ghidra/research/Morph374RebindEvidence.java', 'tools/ghidra/research/Morph374RemovalEvidence.java', 'tools/ghidra/research/Payoff375DetachEvidence.java', 'tools/ghidra/research/Payoff375RoboPolicyEvidence.java', 'tools/ghidra/research/Payoff380ChemicalEvidence.java', 'tools/ghidra/research/Payoff382BuffAliasEvidence.java', 'tools/ghidra/research/Payoff382BuffLifecycleEvidence.java', 'tools/ghidra/research/Payoff382BuffPauseEvidence.java', 'tools/ghidra/research/Payoff382ClockAcceptedEvidence.java', 'tools/ghidra/research/Payoff382ClockCompositionEvidence.java', 'tools/ghidra/research/Payoff382FormPauseEvidence.java', 'tools/ghidra/research/Payoff382MetamorphosisEvidence.java', 'tools/ghidra/research/morph374_freeze.py', 'tools/ghidra/research/morph375_freeze.py', 'tools/ghidra/research/morph380_freeze.py', 'tools/ghidra/research/morph382_freeze.py', 'tools/ghidra/retail_capture_source.py', 'tools/ghidra/verify_wc3_pathing_ethereal357.py', 'tools/ghidra/verify_wc3_pathing_morph372.py', 'tools/ghidra/verify_wc3_pathing_morph374.py', 'tools/ghidra/verify_wc3_pathing_morph375.py', 'tools/ghidra/verify_wc3_pathing_morph380.py', 'tools/ghidra/verify_wc3_pathing_morph382.py', 'tools/ghidra/verify_wc3_pathing_target_normalize218.py', 'tools/ghidra/wc3_morph374_lifetimes.py', 'tools/ghidra/wc3_morph382_semantics.py']
ORIGINAL_BUNDLES={'native': '1696f1d694be4cc756ce60ae9c38a41f69c5c9f4fcd49552a76025a5dbede8a8', 'public-removal-native': 'a9ffafb4c4fee3e0cf12a6cd7581a058d41322b1bf8a6e366f9624c66c4f6319', 'alias-native': '755aa21b64f511a9e51ce6a9f2d6c5907b40cf652cb7d8693edddcfe231a8d62', 'pause-native': 'fc70bd51feb2acea915ab9e8bafc01d05396266937db165c96df5eab713ef906', 'sleep-pause-native': '3aa6f18d2d866081dffbc79a5b72ea0d1ff25c0af81ccd5a99a9c9b0dd557103'}
sha=lambda raw:hashlib.sha256(raw).hexdigest()
def raw(bundle,name):return base64.b64decode(bundle['raw'][name],validate=True)
def resolve(relative,assets=ROOT):
 candidate=Path(assets)/relative
 return candidate if candidate.is_file()else ROOT/relative

def guards(assets=ROOT):
 proof=json.loads(resolve(PROOF,assets).read_text());instructions=proof['instructions'];data=proof['data_guards']
 if len(instructions)!=5271 or len(data)!=35 or instructions.get('6f4c8650')!='c3' or data.get('6fb6fe3c')!='40c6546f':raise ValueError('incomplete concrete AEme guard inventory')
 return instructions,data

def markers(bundle,name,prefix):return re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',raw(bundle,name).decode())
def semantic(family,rows,public,setting):
 fn=getattr(semantics,('ordinary'if family=='detach'else family)+'_semantic')
 fn(rows,public,setting,family=='detach')

def check_group(bundle,expected,family,setting):
 stem,mapbase,prefix,scenes,count=FAMILIES[family];reference=None;projections=[]
 if set(expected['cases'])!={'observe-a','observe-b','control-a'}:raise ValueError('missing whole repeat/control family')
 for mode,contract in expected['cases'].items():
  name=stem+str(setting)+'-'+mode+'.jsonl';preload=name[:-6]+'-preload.txt'
  rows=[json.loads(line)for line in raw(bundle,name).decode().splitlines()];public=markers(bundle,preload,prefix);metadata=rows[0]
  if len(public)!=count or rows[-1]!=dict(event='preload-file',sha256=sha(raw(bundle,preload)),markers=count,complete=True):raise ValueError('incomplete public footer')
  if any(r['event']not in EVENTS|WRAPPER_EVENTS for r in rows):raise ValueError('unknown event; no silent drops')
  if metadata.get('sha256')!=SHA or metadata.get('owned')is not True or metadata.get('mode')!=mode.split('-')[0] or metadata.get('env')!=contract['environment'] or metadata['source_sha256']!=contract['source_sha256']:raise ValueError('capture source/module/controller provenance differs')
  if metadata['source_sha256']['map']!=expected['map_sha256']:raise ValueError('map provenance differs')
  if reference is None:reference=public
  if public!=reference:raise ValueError('literal public repeat/control differs')
  if mode.startswith('observe'):
   if [r['value']for r in rows if r['event']=='marker']!=public:raise ValueError('active/public marker stream differs')
   ends=[r for r in rows if r['event']=='trace-end']
   if len(ends)!=1 or ends[0].get('readOnly')is not True or ends[0]['counts']!=dict(collections.Counter(r['event']for r in rows if 'seq'in r)):raise ValueError('native completeness differs')
   first=next(i for i,r in enumerate(rows)if r['event']=='marker'and ' label=start'in r['value']);module=next(r for r in rows if r['event']=='module');start=int(module['base'],16)
   exceptions=[(i,r)for i,r in enumerate(rows)if r['event']=='native-exception']
   if len(exceptions)!=2 or any(i>=first or r['type']!='system'or start<=int(r['address'],16)<start+metadata['imageSize']for i,r in exceptions):raise ValueError('reported startup exception chronology differs')
   if [r for i,r in exceptions]!=contract['reported_exceptions']:raise ValueError('literal complete exception rows differ')
   semantic(family,rows,public,setting)
   projection,ledger=project(rows)
   if ledger!=contract['retirement_ledger']:raise ValueError('literal object-lifetime ledger differs')
   if projection!=expected['projection']:raise ValueError('full scalar/public/clock/exception projection differs')
   projections.append(projection)
  elif any('seq'in r for r in rows):raise ValueError('observer-free control has observer records')
 if projections[0]!=projections[1]:raise ValueError('complete projected repeats differ')
 return dict(captures=3,scenes=scenes,public_markers=count,projected_events=len(projections[0]),native_events=sum(r['event']not in('marker','native-exception')for r in projections[0]),recorded_system_exceptions=2)

RAW_MANIFEST_SHA256='d4ec4bb6005526d89f84de0b200f98910e4dfe0b288b9ebe7e7fa8f04aac3661'
def runtime(bundle,spec):
 if sha(json.dumps(spec['raw_sha256'],sort_keys=True,separators=(',',':')).encode())!=RAW_MANIFEST_SHA256:raise ValueError('original complete raw inventory differs')
 required={family+str(setting)for family in FAMILIES for setting in(0,1)}
 if set(spec['groups'])!=required:raise ValueError('missing concrete settings/families')
 if set(bundle['raw'])!=set(spec['raw_sha256']):raise ValueError('missing or added original raw records/sources/diagnostics')
 for name,pin in spec['raw_sha256'].items():
  if sha(raw(bundle,name))!=pin:raise ValueError('original full raw file differs: '+name)
 totals=collections.Counter()
 for family in FAMILIES:
  for setting in(0,1):totals.update(check_group(bundle,spec['groups'][family+str(setting)],family,setting))
 if dict(totals)!=dict(captures=36,scenes=38,public_markers=3790,projected_events=8470,native_events=4656,recorded_system_exceptions=24):raise ValueError('complete evidence inventory differs')
 return dict(totals)

def builder_ast(source):
 tree=ast.parse(source)
 for node in tree.body:
  if isinstance(node,ast.Assign)and any(isinstance(target,ast.Name)and target.id=='PARENT'for target in node.targets):
   original=isinstance(node.value,ast.Call)and isinstance(node.value.func,ast.Name)and node.value.func.id=='Path'and len(node.value.args)==1 and isinstance(node.value.args[0],ast.Constant)and node.value.args[0].value=='/run/media/lofcz/ssd_external/GitHub/open-realm/tools/frida/research'
   relocated=isinstance(node.value,ast.Name)and node.value.id=='HERE'
   if not(original or relocated):raise ValueError('unapproved builder source-root grammar')
   node.value=ast.Name(id='CAPTURED_PARENT_SOURCE_ROOT',ctx=ast.Load())
 return ast.dump(tree,include_attributes=False)

def validate(spec,assets=ROOT):
 instructions,data=guards(assets)
 if set(spec.get('pins',{}))!=set(PINS) or spec.get('original_bundle_sha256')!=ORIGINAL_BUNDLES:raise ValueError('missing evidence source/original bundle pins')
 if spec.get('task')!='SEP-01.2'or spec.get('version')!=1 or spec.get('game_sha256')!=SHA or spec.get('projection_contract')!=CONTRACT or spec.get('engine_tests')!=TESTS or spec.get('instructions')!=instructions or spec.get('data_guards')!=data:raise ValueError('complete producer contract differs')
 for name,pin in spec['pins'].items():
  file=resolve(name,assets)
  if sha(file.read_bytes())!=pin and not matches_capture_source(ROOT,name,pin):raise ValueError('evidence source differs: '+name)
 packed=resolve(BUNDLE,assets).read_bytes()
 if sha(packed)!=spec['bundle_sha256']:raise ValueError('original raw bundle differs')
 bundle=json.loads(gzip.decompress(packed));runtime(bundle,spec)
 if set(bundle['original_bundles'])!=set(ORIGINAL_BUNDLES):raise ValueError('missing original accepted bundle')
 accepted={}
 for name,pin in ORIGINAL_BUNDLES.items():
  original=base64.b64decode(bundle['original_bundles'][name],validate=True)
  if sha(original)!=pin:raise ValueError('immutable original bundle differs')
  for path,value in json.loads(gzip.decompress(original))['raw'].items():
   if path in accepted and accepted[path]!=value:raise ValueError('conflicting original raw input')
   accepted[path]=value
 latest=json.loads(gzip.decompress(base64.b64decode(bundle['original_bundles']['sleep-pause-native'],validate=True)))['native_guards']
 if latest!={'instructions':instructions,'data_guards':data}:raise ValueError('complete original function guards differ')
 for path,value in accepted.items():
  if bundle['raw'].get(path)!=value:raise ValueError('original raw source/capture changed: '+path)
 if {name:sha(base64.b64decode(value,validate=True))for name,value in bundle['captured_sources'].items()}!=spec['captured_source_sha256']:raise ValueError('captured source inventory differs')
 if {name:sha(base64.b64decode(value,validate=True))for name,value in bundle['controllers'].items()}!=spec['controller_sha256']or set(bundle['controllers'])!={'queue263-ui.exe','queue263-ui.exe.so'}:raise ValueError('controller inventory differs')
 if sha(base64.b64decode(bundle['map_input'],validate=True))!=spec['base_sha256']:raise ValueError('original map input differs')
 byname=collections.defaultdict(set)
 for name,source in bundle['captured_sources'].items():byname[Path(name).name].add(sha(base64.b64decode(source,validate=True)))
 for name,source in bundle['controllers'].items():byname[name].add(sha(base64.b64decode(source,validate=True)))
 for name in bundle['raw']:byname[Path(name).name].add(sha(raw(bundle,name)))
 for group in spec['groups'].values():
  for mode,case in group['cases'].items():
   for name,pin in case['source_sha256'].items():
    if name!='map'and pin not in byname[name]:raise ValueError('missing captured source/controller bytes: '+name)
 for name in bundle['raw']:
  if not name.endswith('.launch.json'):continue
  launch=json.loads(raw(bundle,name))
  for captured,pin in launch['pins'].items():
   if pin not in byname[Path(captured).name]:raise ValueError('missing launch-pinned bytes: '+captured)
 for name in spec['ported_builders']:
  historical=base64.b64decode(bundle['captured_sources'][name],validate=True)
  if builder_ast(historical)!=builder_ast(resolve(name,assets).read_bytes()):raise ValueError('portable builder changed beyond source-root relocation')
 return bundle

def rebuild(bundle,spec,assets=ROOT,mpqtool=None):
 with tempfile.TemporaryDirectory(prefix='morph382-map-')as directory:
  root=Path(directory);base=root/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input'],validate=True))
  # Builders are colocated with the original probe bytes and guarded parent builder.
  for name,source in bundle['captured_sources'].items():
   if name.startswith('tools/frida/research/'):
    target=root/Path(name).name;target.write_bytes(resolve(name,assets).read_bytes())
  for family,(stem,mapbase,prefix,scenes,count)in FAMILIES.items():
   builder='payoff382'+dict(ordinary='',detach='d',public='r',alias='a',pause='p',sleep='w')[family]+'_make_map.py'
   for setting in(0,1):
    out=root/(mapbase+str(setting)+'.w3m')
    subprocess.run([sys.executable,str(root/builder),'--base',str(base),'--output',str(out),'--mpqtool',str(mpqtool or ROOT/'build/bin/mpqtool'),'--alternate-disable',str(setting)],check=True,stdout=subprocess.DEVNULL)
    expected=json.loads(raw(bundle,mapbase+str(setting)+'.json'));actual=json.loads(out.with_suffix('.json').read_text())
    if sha(out.read_bytes())!=spec['groups'][family+str(setting)]['map_sha256']:raise ValueError('deterministic map reconstruction differs')
    # Preserve original manifests; only builder path portability changes its source hash.
    actual['builder_sha256']=expected['builder_sha256']
    if actual!=expected:raise ValueError('map member/probe/parent manifest differs')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--native-only',action='store_true');p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--assets-root',type=Path,default=ROOT);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(resolve(SPEC,a.assets_root).read_text());bundle=validate(spec,a.assets_root);original_bytes(a.binary,spec['instructions']|spec['data_guards']);rebuild(bundle,spec,a.assets_root)
 result=dict(passed=True,status='native-verified'if a.native_only else'verified',binary_sha256=SHA,instructions=5271,data_ranges=35,**runtime(bundle,spec))
 if not a.native_only:
  import verify_wc3_pathing_work242 as runner
  old=runner.TESTS
  try:runner.TESTS=TESTS;result['engine']=runner.run_engine(a.test_binary,a.data,a.report)
  finally:runner.TESTS=old
  result.update(test_binary_sha256=sha(a.test_binary.read_bytes()),game_library_sha256=sha((a.test_binary.parent.parent/'lib/libgame-wc3-test.so').read_bytes()))
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
