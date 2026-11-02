#!/usr/bin/env python3
"""Verify Freeze-family class, counter, order and acquisition lifetimes."""
import argparse,base64,copy,gzip,hashlib,json,re,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_group341 import projection as common_projection
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
from research.group359_classes import original as original_classes
ROOT=Path(__file__).resolve().parents[2]
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SPEC=ROOT/'tools/ghidra/fixtures/retail-group359-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group359-inputs-1.27.json.gz'
TESTS=['wc3_order_lifecycle.*','wc3_ability_dispatch.*','wc3_combat.*','wc3_item_lifecycle.*',
 'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map',
 'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
def digest(data):return hashlib.sha256(data).hexdigest()
def public(text,prefix):return re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',text)
def projection(rows):
    rows=copy.deepcopy(rows)
    for row in rows:
        for field in ('before','after'):
            if isinstance(row.get(field),dict)and 'unit'in row[field]:row['actor']=row[field].pop('unit')
    return common_projection(rows)
def semantic(rows,index):
    starts=[r for r in rows if r.get('event')=='stun-initialize']
    ends=[r for r in rows if r.get('event')=='stun-initialize-end']
    inverse=[r for r in rows if r.get('event')=='stun-remove-end']
    if len(starts)!=(7 if index==0 else 2)or len(ends)!=len(starts)or len(inverse)!=len(starts):raise ValueError('missing concrete buff lifetime')
    for begin,end,remove in zip(starts,ends,inverse):
        orders=begin['vtable']=='afa618'
        if begin['vtable']not in('afa618','b3acac')or begin['parameters']!=[int(orders),0,0,1]:raise ValueError('wrong initializer flags/class')
        expected=(0,1)if orders else(1,0)
        if (end['after']['execution'],end['after']['orders'])!=expected:raise ValueError('wrong acquired counters')
        if remove['after']['execution']or remove['after']['orders']:raise ValueError('missing counter inverse')
        if orders and(end['after']['move']!={'execution':1,'orders':1}or remove['after']['move']!={'execution':0,'orders':0}):raise ValueError('wrong Move owner counters')
def runtime(experiments,spec):
    if len(experiments)!=3 or len(spec['experiments'])!=3:raise ValueError('missing public experiment')
    for index,(experiment,contract)in enumerate(zip(experiments,spec['experiments'])):
        cases=experiment['cases']
        if len(cases)!=3 or [c['rows'][0].get('mode')for c in cases]!=['observe','observe','control']:raise ValueError('missing repeats/control')
        for i,case in enumerate(cases):
            rows=case['rows'];meta=rows[0];marks=public(case['preload'],contract['prefix'])
            if(meta.get('task')!='GROUP-03.2'or meta.get('sha256')!=SHA or not meta.get('owned')or
               meta.get('launcher')!='owned-wine-popen'or not isinstance(meta.get('linux_pid'),int)or meta.get('env')not in('B','C')or
               meta.get('source_sha256')!=contract['capture_sources'][i]or meta['source_sha256']['map']!=contract['map_sha256']or
               any(r.get('type')=='error'or r.get('event')in('compile-error','trace-failed')for r in rows)):raise ValueError('capture provenance/failure')
            if marks!=contract['public']or len(marks)!=(110 if index==0 else 26)or rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':len(marks),'complete':True}:raise ValueError('incomplete/changed public stream')
            if meta['mode']=='control':
                if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
                continue
            end=[r for r in rows if r.get('event')=='trace-end']
            if(len(end)!=1 or not end[0].get('installed')or not end[0].get('readOnly')or end[0]['depth']or end[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r))):raise ValueError('unfinished observer')
            if projection(rows)!=contract['events']or [r['value']for r in rows if r.get('event')=='marker']!=marks:raise ValueError('native stream differs')
            semantic(rows,index)
            if index==2:
                completed=[m for m in marks if 'label=frozen 'in m]
                if len(completed)!=2 or any('freeze=0'not in m or 'life=0.0000'not in m for m in completed):raise ValueError('lethal Freeze must be removed by death')
                for begin,remove in zip([r for r in rows if r.get('event')=='stun-initialize'],[r for r in rows if r.get('event')=='stun-remove']):
                    if begin['tick']!=remove['tick']:raise ValueError('death inverse must occur during the lethal hit')
        if digest(base64.b64decode(experiment['map_input']))!=contract['base_sha256']:raise ValueError('unknown map base')
    return dict(captures=9,scenes=16,public_markers=162,native_events=2209)
def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='GROUP-03.2'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=808 or len(spec.get('classes',[]))!=724 or [len(e['events'])for e in spec.get('experiments',[])]!=[2141,34,34]):raise ValueError('incomplete contract')
    for p,h in spec['pins'].items():
        if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle changed')
    experiments=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(experiments,spec);return experiments

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');parser.add_argument('--data',type=Path,default=ROOT/'build/tests');a=parser.parse_args()
    if a.report.exists():parser.error('fresh report required')
    spec=json.loads(SPEC.read_text());experiments=validate(spec);original_bytes(a.binary,spec['instructions'])
    if original_classes(a.binary)!=spec['classes']:raise ValueError('CAbility primary-vtable inventory differs')
    with tempfile.TemporaryDirectory(prefix='group359-')as tmp:
        for i,(experiment,contract)in enumerate(zip(experiments,spec['experiments'])):
            base=Path(tmp)/f'base{i}.w3m';base.write_bytes(base64.b64decode(experiment['map_input']));output=Path(tmp)/f'map{i}.w3m'
            subprocess.run([sys.executable,str(ROOT/'tools/frida/research/group341_make_map.py'),'--base',str(base),'--output',str(output),'--probe',str(ROOT/contract['probe'])],check=True,stdout=subprocess.DEVNULL)
            if digest(output.read_bytes())!=contract['map_sha256']:raise ValueError('map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=808,classes=724,**runtime(experiments,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
