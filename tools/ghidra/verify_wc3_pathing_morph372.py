#!/usr/bin/env python3
"""Verify Submerge's complete native passive-availability stream and inverse lifecycle."""
import argparse,base64,copy,gzip,json,re,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_ethereal357 import ROOT,SHA,digest
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-morph372-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-morph372-inputs-1.27.json.gz'
TESTS=['wc3_unit.morph372*','wc3_unit.submerge*','wc3_unit.burrow366*','wc3_repulsion_policy.*',
       'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
CAPTURE_SOURCES=['separation366_loaded_capture.py','morph372_observer.js','group338_make_map.py',
                 'group338_probe.j','queue263_ui_input.c','order0110_ui_input.c']
GUARDS=['retail-submerge360-1.27.json','retail-burrow366-passives-1.27.json']
PIN_PATHS=['tools/frida/research/'+name for name in CAPTURE_SOURCES]+[
    'tools/frida/research/morph372_make_map.py','tools/frida/research/morph372_probe.j',
    'tools/ghidra/research/morph372_freeze.py','tools/ghidra/verify_wc3_pathing_morph372.py',
    'tools/ghidra/retail_capture_source.py','tools/ghidra/verify_wc3_pathing_ethereal357.py',
    'tools/ghidra/verify_wc3_pathing_target_normalize218.py',
    'tools/ghidra/research/Submerge360Evidence.java','tools/ghidra/research/Burrow366AvailabilityEvidence.java',
    'tools/ghidra/research/Burrow366DetachEvidence.java']+['tools/ghidra/fixtures/'+name for name in GUARDS]
NATIVE_EVENTS={'module','marker','refresh-enter','configuration','refresh-leave',
    'invisibility-acquire-enter','invisibility-acquire-leave','invisibility-release-enter','invisibility-release-leave',
    'morph-begin-enter','morph-begin-leave','acquire-enter','acquire-leave','transition-timer',
    'morph-end-enter','morph-end-leave','release-enter','release-leave','submerge-category',
    'morph-form-disable-enter','morph-form-disable-leave','morph-form-enable-enter','morph-form-enable-leave',
    'ability-disable-remove-enter','ability-disable-remove-leave','ability-disable-add-enter','ability-disable-add-leave'}
WRAPPER_EVENTS={'native-exception','metadata','owned-loading-input','loading-key','trace-end','preload-file','control-start-file'}
PROJECTION_CONTRACT=dict(literal_unit_offsets=[0x118,0x11c,0x120,0x124],pointer_unit_offsets=[0x128,0x12c],
    pointer_mapping='whole-capture bijection preserving aliases, nulls, sentinels and reuse',
    proof='6696e0 and694b20 dereference Unit128 and Unit12c as refcounted polymorphic objects; '
          '128 receives058120 modifier,12c receives058090 threshold listener. '
          'All other words, clocks, stable identities and native sequence remain literal.',
    proof_instruction_sites=['6f66981e','6f669828','6f66982f','6f669836','6f669788','6f669869',
        '6f66987d','6f669883','6f669889','6f669890','6f669897','6f694c36','6f694c4e','6f694be3','6f694caf'])

def projection(rows):
    """Keep all native rows and literal words, clocks, identities and sequence.

    Pre-start system exceptions remain literal. Only the module-base
    announcement is excluded. FloatProp words 0..3 stay
    literal, including registry slot/generation. Guarded 6696e0/694b20 identify
    words4/5 as heap modifier/listener pointers; their whole-capture bijection
    retains null/sentinel values and aliases, including pointer reuse.
    """
    pointers={};result=[]
    for row in rows:
        if ('seq'not in row and row.get('event')!='native-exception')or row['event']=='module':continue
        value=copy.deepcopy(row)
        for key in('before','after','state'):
            state=value.get(key)
            if not isinstance(state,dict)or 'invisibilityScalar'not in state:continue
            words=state['invisibilityScalar']
            if len(words)!=6:raise ValueError('incomplete FloatProp scalar')
            for i in(4,5):
                word=words[i]
                if word not in(0,0xffffffff):
                    if word not in pointers:pointers[word]=len(pointers)+1
                    words[i]=['scalar-owner-pointer',pointers[word]]
        result.append(value)
    return result

def guard_inventory():
    instructions={}
    for name in GUARDS:
        source=json.loads((ROOT/'tools/ghidra/fixtures'/name).read_text())
        if source['game_sha256']!=SHA:raise ValueError('wrong static binary')
        for address,raw in source['instructions'].items():
            if address in instructions and instructions[address]!=raw:raise ValueError('conflicting static guard')
            instructions[address]=raw
    return instructions

def runtime(bundle,spec):
    cases=bundle['cases']
    if len(cases)!=3 or [c['rows'][0].get('mode')for c in cases]!=['observe','observe','control']:
        raise ValueError('missing independent repeat/control')
    expected_sources={name:spec['pins']['tools/frida/research/'+name]for name in CAPTURE_SOURCES}
    expected_sources['map']=spec['map_sha256']
    controllers=bundle['controllers']
    if set(controllers)!={'queue263-ui.exe','queue263-ui.exe.so'}:raise ValueError('missing controller inputs')
    expected_sources.update({name:digest(base64.b64decode(raw,validate=True))for name,raw in controllers.items()})
    for i,case in enumerate(cases):
        rows=case['rows'];meta=rows[0]
        marks=re.findall(r'call Preload\( "(M372 [^"\r\n]*)" \)',case['preload'])
        if(meta.get('event')!='metadata'or meta.get('sha256')!=SHA or meta.get('owned')is not True or
           meta.get('env')not in('B','C')or meta.get('source_sha256')!=expected_sources or
           meta.get('source_sha256')!=spec['capture_sources'][i]or meta.get('map')!='Maps/Morph372b.w3m'or
           meta.get('timestamp')!=1481263518 or meta.get('imageSize')!=15130624 or meta.get('prefix')!='M372 'or meta.get('launcher')!='owned-wine-popen'or meta.get('input')!=[]):
            raise ValueError('capture provenance differs')
        if any(r.get('event')not in NATIVE_EVENTS|WRAPPER_EVENTS or r.get('type')=='error'for r in rows):
            raise ValueError('unexpected/failed capture event')
        if(marks!=spec['public']or len(marks)!=311 or 'tick=299 scene=4 label=complete'not in marks[-1]or
           rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':311,'complete':True}):
            raise ValueError('incomplete/changed public behavior')
        if i==2:
            if any('seq'in r or r.get('event')in NATIVE_EVENTS|{'trace-end','native-exception'}for r in rows):
                raise ValueError('instrumented control')
            continue
        exceptions=[r for r in rows if r.get('event')=='native-exception']
        first=next(j for j,r in enumerate(rows)if r.get('event')=='marker')
        if(len(exceptions)!=2 or any(r.get('type')!='system'or r.get('address')!='0x7b55b6d9'for r in exceptions)or
           any(r.get('event')=='native-exception'for r in rows[first:])):
            raise ValueError('changed/in-scenario native exception')
        native=[r for r in rows if 'seq'in r]
        if([r['seq']for r in native]!=list(range(1,762))or native[0].get('event')!='module'or
           any(r['event']not in NATIVE_EVENTS for r in native)or
           any(r['event']in NATIVE_EVENTS and 'seq'not in r for r in rows)):
            raise ValueError('incomplete native sequence')
        ends=[r for r in rows if r.get('event')=='trace-end']
        if(len(ends)!=1 or ends[0].get('readOnly')is not True or
           ends[0].get('counts')!=dict(Counter(r['event']for r in native))):raise ValueError('unfinished/read-write observer')
        if projection(rows)!=spec['events']or [r['value']for r in native if r['event']=='marker']!=marks:
            raise ValueError('complete native stream differs')
    return dict(captures=3,scenes=5,public_markers=311,native_events=449,recorded_system_exceptions=2,projected_events=762)

def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       len(spec.get('events',[]))!=762 or spec.get('engine_tests')!=TESTS or
       set(spec.get('pins',{}))!=set(PIN_PATHS)or spec.get('projection_contract')!=PROJECTION_CONTRACT):raise ValueError('incomplete Morph372 inventory')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('changed source '+path)
    if(spec['instructions']!=guard_inventory()or len(spec['instructions'])!=1474 or
       any(site not in spec['instructions']for site in PROJECTION_CONTRACT['proof_instruction_sites'])):raise ValueError('changed static inventory')
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed frozen input')
    bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle,spec)
    if digest(base64.b64decode(bundle['map_input'],validate=True))!=spec['base_sha256']:raise ValueError('changed map input')
    manifest=bundle['map_manifest']
    if(manifest['base_sha256']!=spec['base_sha256']or manifest['map_sha256']!=spec['map_sha256']or
       manifest['probe_sha256']!=spec['pins']['tools/frida/research/morph372_probe.j']or
       manifest['builder_sha256']!=spec['pins']['tools/frida/research/morph372_make_map.py']):
        raise ValueError('map provenance differs')
    return bundle

def rebuild(bundle,spec):
    with tempfile.TemporaryDirectory(prefix='morph372-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']));output=Path(tmp)/'probe.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/morph372_make_map.py'),
            '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
            '--empty-ability-units','nmyr','nmys','nnrg','nnrs','nsnp','nsbs'],check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs')
        if json.loads(output.with_suffix('.json').read_text())!=bundle['map_manifest']:
            raise ValueError('reconstructed map members/provenance differ')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions']);rebuild(bundle,spec)
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),**runtime(bundle,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
