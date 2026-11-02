#!/usr/bin/env python3
"""Verify configured common Morph availability, exact native frontiers and enemy visibility."""
import argparse,base64,gzip,json,re,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_morph372 import ROOT,SHA,digest,projection as whole_capture_projection,guard_inventory as parent_guards,NATIVE_EVENTS,WRAPPER_EVENTS
from wc3_morph374_lifetimes import projection,project,CONTRACT as PROJECTION_CONTRACT
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-morph374-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-morph374-inputs-1.27.json.gz'
TESTS=['wc3_unit.morph374*','wc3_unit.morph372*','wc3_unit.burrow366*','wc3_unit.morph331*','wc3_repulsion_policy.*',
       'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
CAPTURE_SOURCES=['separation366_loaded_capture.py','morph374_observer.js','group338_make_map.py',
                 'group338_probe.j','queue263_ui_input.c','order0110_ui_input.c']
PIN_PATHS=['tools/frida/research/'+n for n in CAPTURE_SOURCES]+[
    'tools/frida/research/morph374b_make_map.py','tools/frida/research/morph374b_probe.j',
    'tools/frida/research/morph374_make_map.py','tools/frida/research/morph374_probe.j',
    'tools/frida/research/morph330_make_map.py','tools/ghidra/verify_wc3_pathing_morph372.py',
    'tools/ghidra/research/morph374_freeze.py','tools/ghidra/verify_wc3_pathing_morph374.py',
    'tools/ghidra/retail_capture_source.py','tools/ghidra/verify_wc3_pathing_ethereal357.py',
    'tools/ghidra/verify_wc3_pathing_target_normalize218.py',
    'tools/ghidra/fixtures/retail-submerge360-1.27.json','tools/ghidra/fixtures/retail-burrow366-passives-1.27.json',
    'tools/ghidra/fixtures/retail-group359-1.27.json','tools/ghidra/wc3_morph374_lifetimes.py',
    'tools/ghidra/fixtures/retail-morph374-lifetime-ghidra-1.27.json','tools/ghidra/research/Morph374LifetimeEvidence.java',
    'tools/ghidra/fixtures/retail-morph374-rebind-ghidra-1.27.json','tools/ghidra/research/Morph374RebindEvidence.java',
    'tools/ghidra/fixtures/retail-morph374-removal-ghidra-1.27.json','tools/ghidra/research/Morph374RemovalEvidence.java']
EVENTS=NATIVE_EVENTS|{'morph-policy-enter','morph-policy-leave'}
CLASSES=['aee52c','afe6ec'];BASE_TYPES=[0x65646f63,0x65646f74];ALT_TYPES=[0x6564636d,0x6564746d]
MARK=re.compile(r'M374B setting=(-?\d+) invisible=([01]) tick=(\d+) scene=(\d+) label=([a-z-]+) accepted=([01]) type=(\d+) morph=(\d+) passive=(\d+) order=(\d+)')

def guard_inventory():
    guards=parent_guards();extra=json.loads((ROOT/'tools/ghidra/fixtures/retail-morph374-lifetime-ghidra-1.27.json').read_text())
    if extra['game_sha256']!=SHA or len(extra['functions'])!=7 or len(extra['instructions'])!=464:
        raise ValueError('incomplete saved lifetime proof')
    for site,raw in extra['instructions'].items():
        if site in guards and guards[site]!=raw:raise ValueError('conflicting instruction guards')
        guards[site]=raw
    rebind=json.loads((ROOT/'tools/ghidra/fixtures/retail-morph374-rebind-ghidra-1.27.json').read_text())
    if rebind['game_sha256']!=SHA or len(rebind['functions'])!=8 or len(rebind['instructions'])!=1632:
        raise ValueError('incomplete saved rebind/parser proof')
    for site,raw in rebind['instructions'].items():
        if site in guards and guards[site]!=raw:raise ValueError('conflicting rebind guards')
        guards[site]=raw
    removal=json.loads((ROOT/'tools/ghidra/fixtures/retail-morph374-removal-ghidra-1.27.json').read_text())
    if removal['game_sha256']!=SHA or len(removal['functions'])!=7 or len(removal['instructions'])!=479:
        raise ValueError('incomplete saved passive-removal proof')
    for site,raw in removal['instructions'].items():
        if site in guards and guards[site]!=raw:raise ValueError('conflicting passive-removal guards')
        guards[site]=raw
    if guards.get('6f670d54')!='e827aa0100' or len(guards)!=3513 or any(site not in guards for site in PROJECTION_CONTRACT['proof_instruction_sites']):
        raise ValueError('missing exact lifetime proof instructions')
    return guards

def slot_inventory():
    slots=json.loads((ROOT/'tools/ghidra/fixtures/retail-morph374-removal-ghidra-1.27.json').read_text())['vtable_slots']
    if slots!={'6fb674ec':'10cf526f','6fb6750c':'b076406f','6fb67514':'80a43f6f','6fb67528':'00e6626f'}:
        raise ValueError('changed concrete Apiv retirement slot proof')
    return slots

def rows(case):return [json.loads(line)for line in case['raw_jsonl'].splitlines()]
def public(preload):return re.findall(r'call Preload\( "(M374B [^"\r\n]*)" \)',preload)
def marker_fields(value):
    match=MARK.fullmatch(value)
    if not match:raise ValueError('unexpected public marker shape')
    names=['setting','invisible','tick','scene','label','accepted','type','morph','passive','order']
    return {k:v if k=='label'else int(v)for k,v in zip(names,match.groups())}
def one(items,label):
    if len(items)!=1:raise ValueError('expected one '+label)
    return items[0]

def semantics(native,marks,setting):
    """Check independently authored/public contracts in addition to complete raw equality."""
    if marks[0]!='M374B tick=0 label=start':raise ValueError('missing public start')
    parsed=[marker_fields(v)for v in marks[1:]];policy=1 if setting=='1'else 0
    if any(m['setting']!=(-1 if setting=='default'else int(setting))for m in parsed):raise ValueError('wrong authored setting')
    queries=[r for r in native if r['event']=='morph-policy-leave']
    if len(queries)!=26 or any(r.get('result')!=policy for r in queries):raise ValueError('common policy return differs')
    timers=[r for r in native if r['event']=='transition-timer']
    wanted={11:0x3ec00000,34:0x3ec00000,81:0x3ec00000,104:0x3ec00000,174:0x3ec00000,
            221:0x3ec00000,244:0x3fb00000,291:0x3ec00000,314:0x3fb00000,384:0x3fb00000}
    if len(timers)!=10 or {r['tick']:r['delay']for r in timers}!=wanted or any(r.get('caller')!='569f37'for r in timers):
        raise ValueError('exact .375 transition/1.375 landing timer differs')
    for scene in range(6):
        family=scene//3;mode=scene%3;start=scene*70
        seen=[m for m in parsed if m['scene']==scene]
        def sample(age):return one([m for m in seen if m['tick']==start+age and m['label']=='sample'],'sample')
        if mode==0:
            before=one([m for m in seen if m['label']=='pre-forward-visibility'],'positive enemy control')
            if before['tick']!=start+11 or before['invisible']!=1:raise ValueError('missing positive enemy visibility witness')
        if sample(30)['invisible']!=1-policy or sample(30)['type']!=ALT_TYPES[family]:
            raise ValueError('alternate form passive availability differs')
        if sample(55)['invisible']!=1 or sample(55)['type']!=BASE_TYPES[family]or sample(55)['order']!=0:
            raise ValueError('post-completion fade did not restore invisibility')
        removed=one([m for m in seen if m['label']=='remove-passive'],'passive removal')
        if removed['invisible'] or removed['passive']:raise ValueError('passive removal did not clear visibility')
        scene_rows=[r for r in native if start<=r['tick']<start+70]
        acquire=one([r for r in scene_rows if r['event']=='morph-form-disable-enter'],'form acquire')
        acquired=one([r for r in scene_rows if r['event']=='morph-form-disable-leave'],'form acquired')
        release=one([r for r in scene_rows if r['event']=='morph-form-enable-enter'],'form release')
        released=one([r for r in scene_rows if r['event']=='morph-form-enable-leave'],'form released')
        if any(r['availability']['class']!=CLASSES[family]for r in [acquire,acquired,release,released]):
            raise ValueError('wrong concrete Morph class')
        if(acquire['state']['rawcode']!=ALT_TYPES[family]or release['state']['rawcode']!=BASE_TYPES[family]or
           bool(acquired['availability']['flags']&0x100000)!=bool(policy)or released['availability']['flags']&0x100000):
            raise ValueError('held alternate owner/inverse differs')
        if mode==0:
            state=acquire['state']
            if(state['invisibilityDepth']!=1 or not state['flags']&0x1000000 or state['invisibilityScalar'][4:]!=[0,0]):
                raise ValueError('retained passive rebind did not force scalar completion before acquire')
        if mode==2:
            if acquire['tick']!=start:raise ValueError('initial alternate acquisition is not synchronous')
        else:
            forward=one([r for r in scene_rows if r['event']=='marker'and 'label=forward 'in r['value']],'forward public return')
            begin=one([r for r in scene_rows if r['event']=='morph-begin-enter'and r['tick']<start+30],'forward begin')
            finish=one([r for r in scene_rows if r['event']=='morph-end-enter'and r['tick']<start+30],'forward completion')
            if family==0 and not(forward['seq']<finish['seq']<acquire['seq']):raise ValueError('Bear acquire frontier differs')
            if family==1 and not(acquired['seq']<begin['seq']<forward['seq']):raise ValueError('takeoff acquire frontier differs')
        backward=one([r for r in scene_rows if r['event']=='morph-end-enter'and r['tick']>start+30],'reverse completion')
        if not(backward['seq']<release['seq']<released['seq']):raise ValueError('release preceded reverse completion')
        walk=[r for r in scene_rows if release['seq']<r['seq']<released['seq']and r.get('availability',{}).get('class')=='b67448']
        if policy:
            before=one([r for r in walk if r['event']=='ability-disable-remove-enter'],'passive inverse entry')
            after=one([r for r in walk if r['event']=='ability-disable-remove-leave'],'passive inverse leave')
            if [before['availability'][k]for k in('execution','orders')]!=[1,1]or [after['availability'][k]for k in('execution','orders')]!=[0,0]:
                raise ValueError('passive counted inverse differs')
            fades=[r for r in scene_rows if release['seq']<r['seq']<released['seq']and r['event']=='invisibility-acquire-leave']
            if len(fades)!=1 or fades[0]['after']['invisibilityDepth']!=1:raise ValueError('inverse did not restart passive fade')
            first=next(m for m in seen if m['label']=='sample'and m['tick']>=start+35 and m['invisible'])
            if first['tick']-start!=(45 if family==0 else 55):raise ValueError('later fade publication frontier differs')
        elif walk:raise ValueError('disabled setting changed passive availability')
    return dict(policy_queries=26,transition_timers=10,positive_enemy_controls=2,restored_passive_scenes=6)

def check_group(cases,spec,setting,controllers,pins):
    if len(cases)!=3:raise ValueError('two observations and one control required per setting')
    allrows=[rows(c)for c in cases]
    if [r[0].get('mode')for r in allrows]!=['observe','observe','control']:raise ValueError('repeat/control modes differ')
    if len({r[0].get('linux_pid')for r in allrows})!=3:raise ValueError('captures must own independent processes')
    expected={n:pins['tools/frida/research/'+n]for n in CAPTURE_SOURCES}
    expected.update({n:digest(base64.b64decode(b,validate=True))for n,b in controllers.items()});expected['map']=spec['map_sha256']
    for index,(case,rs)in enumerate(zip(cases,allrows)):
        meta=rs[0];marks=public(case['preload'])
        if digest(case['raw_jsonl'].encode())!=spec['capture_sha256'][index]:raise ValueError('changed original raw capture')
        if(meta.get('sha256')!=SHA or meta.get('owned')is not True or meta.get('source_sha256')!=expected or
           meta.get('map','').replace('\\','/')!='Maps/Morph374b-'+setting+'.w3m'or meta.get('prefix')!='M374B 'or
           meta.get('launcher')!='owned-wine-popen'or meta.get('input')!=[]or meta.get('timestamp')!=1481263518 or meta.get('imageSize')!=15130624):
            raise ValueError('capture provenance differs')
        if marks!=spec['public']or len(marks)!=440 or 'tick=419 scene=5 label=complete'not in marks[-1]:raise ValueError('public behavior/footer differs')
        if rs[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':440,'complete':True}:raise ValueError('incomplete preload')
        if any(r.get('event')not in EVENTS|WRAPPER_EVENTS or r.get('type')=='error'for r in rs):raise ValueError('failed/unknown capture event')
        if index==2:
            if any('seq'in r or r.get('event')in EVENTS|{'trace-end','native-exception'}for r in rs):raise ValueError('instrumented control')
            continue
        native=[r for r in rs if 'seq'in r];exceptions=[r for r in rs if r.get('event')=='native-exception']
        expected_count=955 if setting=='1'else 931
        first=next(i for i,r in enumerate(rs)if r.get('event')=='marker')
        if(len(exceptions)!=2 or any(r.get('type')!='system'or r.get('address')!='0x7b55b6d9'for r in exceptions)or
           any(r.get('event')=='native-exception'for r in rs[first:])):raise ValueError('changed/in-scenario exception')
        if([r['seq']for r in native]!=list(range(1,expected_count+1))or native[0]['event']!='module'or
           any(r['event']in EVENTS and 'seq'not in r for r in rs)):raise ValueError('incomplete native sequence')
        ends=[r for r in rs if r.get('event')=='trace-end']
        if len(ends)!=1 or ends[0].get('readOnly')is not True or ends[0].get('counts')!=dict(Counter(r['event']for r in native)):
            raise ValueError('unfinished/read-write observer')
        projected,ledger=project(rs)
        if projected!=spec['events']or ledger!=spec['holder_ledgers'][index]or [r['value']for r in native if r['event']=='marker']!=marks:
            raise ValueError('complete native stream/literal clear ledger differs')
        semantics(native,marks,setting)
    first=whole_capture_projection(allrows[0]);second=whole_capture_projection(allrows[1])
    if len(first)!=len(second)or sum(a!=b for a,b in zip(first,second))!=spec['whole_capture_allocator_variance_rows']:
        raise ValueError('original cross-lifetime allocation variance changed')
    return dict(captures=3,scenes=6,public_markers=440,native_events=514 if setting=='1'else 490,
                recorded_system_exceptions=2,projected_events=956 if setting=='1'else 932)

def runtime(bundle,spec):
    groups=bundle['settings']
    if set(groups)!=set(spec['settings'])or set(groups)!={'default','0','1'}:
        raise ValueError('missing configured setting controls')
    if set(bundle['controllers'])!={'queue263-ui.exe','queue263-ui.exe.so'}:raise ValueError('missing controllers')
    details={key:check_group(cases,spec['settings'][key],key,bundle['controllers'],spec['pins'])for key,cases in groups.items()}
    names={d['name']for d in bundle['diagnostics']}
    if len(names)!=len(bundle['diagnostics'])or not {'default-observe-a.jsonl','viewer-0-observe-b.jsonl','0-observe-a.jsonl','1-observe-a.jsonl'}<=names:
        raise ValueError('missing original failed/shared-viewer diagnostics')
    for diagnostic,pin in zip(bundle['diagnostics'],spec['diagnostics']):
        if(diagnostic['name']!=pin['name']or digest(diagnostic['raw_jsonl'].encode())!=pin['raw_sha256']or
           (None if diagnostic['preload']is None else digest(diagnostic['preload'].encode()))!=pin['preload_sha256']):
            raise ValueError('changed retained diagnostic/failure')
    if len(bundle['diagnostics'])!=len(spec['diagnostics']):raise ValueError('missing retained diagnostic/failure')
    totals={key:sum(item[key]for item in details.values())for key in next(iter(details.values()))}
    return dict(settings=len(details),**totals,evidence=details,retained_diagnostics=len(bundle['diagnostics']))

def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       spec.get('engine_tests')!=TESTS or spec.get('projection_contract')!=PROJECTION_CONTRACT or spec.get('vtable_slots')!=slot_inventory() or set(spec.get('pins',{}))!=set(PIN_PATHS)):
        raise ValueError('incomplete common Morph inventory')
    for path,expected in spec['pins'].items():
        if not matches_capture_source(ROOT,path,expected):raise ValueError('changed source '+path)
    if spec['instructions']!=guard_inventory()or len(spec['instructions'])!=3513:raise ValueError('changed complete-function guards')
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed frozen bundle')
    bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle,spec)
    if digest(base64.b64decode(bundle['map_input'],validate=True))!=spec['base_sha256']:raise ValueError('changed base map')
    for key,manifest in bundle['manifests'].items():
        if(manifest['base_sha256']!=spec['base_sha256']or manifest['map_sha256']!=spec['settings'][key]['map_sha256']or
           manifest['alternate_disable']!=key or manifest['builder_sha256']!=spec['pins']['tools/frida/research/morph374b_make_map.py']or
           manifest['probe_source_sha256']!=spec['pins']['tools/frida/research/morph374b_probe.j']or
           manifest['parent_builder_sha256']!=spec['pins']['tools/frida/research/morph330_make_map.py']):raise ValueError('changed map provenance')
    if set(bundle['manifests'])!=set(bundle['settings']):raise ValueError('missing map manifest')
    return bundle

def rebuild(bundle,spec):
    with tempfile.TemporaryDirectory(prefix='morph374-')as directory:
        temp=Path(directory);base=temp/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']))
        for key in bundle['settings']:
            output=temp/(key+'.w3m')
            subprocess.run([sys.executable,str(ROOT/'tools/frida/research/morph374b_make_map.py'),'--base',str(base),
                '--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),'--alternate-disable',key],check=True,stdout=subprocess.DEVNULL)
            if digest(output.read_bytes())!=spec['settings'][key]['map_sha256']or json.loads(output.with_suffix('.json').read_text())!=bundle['manifests'][key]:
                raise ValueError('deterministic map/member reconstruction differs')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests')
    p.add_argument('--native-only',action='store_true',help='Verify original evidence only; report cannot certify engine parity');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions']|spec['vtable_slots']);rebuild(bundle,spec)
    result=dict(passed=True,status='native-verified'if a.native_only else'verified',binary_sha256=SHA,instructions=3513,vtable_slots=4,**runtime(bundle,spec))
    if not a.native_only:
        import verify_wc3_pathing_work242 as runner
        old=runner.TESTS
        try:runner.TESTS=TESTS;result['engine']=runner.run_engine(a.test_binary,a.data,a.report)
        finally:runner.TESTS=old
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
