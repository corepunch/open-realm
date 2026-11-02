#!/usr/bin/env python3
"""Verify stock buff attachment and the concrete Sleep suspension lifecycle."""
import argparse,base64,gzip,json,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_group359 import ROOT,SHA,digest,public,original_bytes
from research.group367_typed import projection
SPEC=ROOT/'tools/ghidra/fixtures/retail-group367-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group367-inputs-1.27.json.gz'
TESTS=['wc3_order_lifecycle.group367_sleep_initial_pause_damage_and_saved_queued_move','wc3_order_lifecycle.group367_sleep_script_pause_composition_and_saved_guard_clock','wc3_order_lifecycle.group367_sleep_paused_refresh_keeps_owner_and_rearms_primary']
def nested_semantic(rows):
    marks=[r['value']for r in rows if r.get('event')=='marker']
    def mark(scene,label):
        found=[m for m in marks if 'scene='+str(scene)+' 'in m and 'label='+label+' 'in m]
        if len(found)!=1:raise ValueError('missing nested marker')
        return found[0]
    for scene in (0,1):
        if 'sleep=1 pause=1 stun=0 order=851973'not in mark(scene,'victim-unpause-after')or 'paused=0'not in mark(scene,'victim-unpause-after'):raise ValueError('nested Unpause released Sleep owner')
        if 'result=0'not in mark(scene,'move-after'):raise ValueError('script pause admitted Move')
        if 'sleep=1 pause=1'not in mark(scene,'damage-late-after')or 'life=420.0000'not in mark(scene,'damage-late-after'):raise ValueError('script pause did not retain initial damage guard clock')
    for scene in (2,3):
        if 'sleep=0 pause=0 stun=0 order=851973'not in mark(scene,'remove-after')or 'paused=1'not in mark(scene,'remove-after'):raise ValueError('buff inverse released scripted pause')
        if 'result=0'not in mark(scene,'move-late-after'):raise ValueError('remaining script pause admitted Move')
    if [r['word']for r in rows if r.get('event')=='sleep-main-duration']!=[0x41a00000]*4 or [r['word']for r in rows if r.get('event')=='sleep-duration-adjustment']!=[0]*4 or [r['duration']for r in rows if r.get('event')=='sleep-pause-source']!=[0x40000000]*4:raise ValueError('actual initializer durations differ')
    for scene in range(4):
        depths=[r['after']['unit']['suspensionDepth']for r in rows if r.get('event')=='suspension-push-end'and r['scene']==scene and r['after']['unit']['owner']==2]
        if max(depths)!=(3 if scene in(0,1,3)else 2):raise ValueError('missing separate internal suspension contributions')
        pops=[r for r in rows if r.get('event')=='suspension-pop-end'and r['scene']==scene and r['after']['unit']['owner']==2]
        if not pops or pops[-1]['after']['unit']['suspensionDepth']!=0:raise ValueError('unbalanced final suspension')
    projection(rows)

def refresh_semantic(rows):
    marks=[r['value']for r in rows if r.get('event')=='marker']
    def mark(tick,label):
        found=[m for m in marks if 'tick='+str(tick)+' scene=3 label='+label+' 'in m]
        if len(found)!=1:raise ValueError('missing paused refresh marker')
        return found[0]
    if 'result=1'not in mark(315,'guard-remove-after')or 'sleep=1 pause=0'not in mark(315,'guard-remove-after'):raise ValueError('guard inverse absent')
    if 'result=1'not in mark(320,'refresh-after'):raise ValueError('refresh not admitted')
    if 'sleep=0 pause=1 order=851973 paused=1'not in mark(355,'continuation'):raise ValueError('paused main clock did not expire')
    if 'sleep=0 pause=0 order=851986 paused=0'not in mark(385,'continuation'):raise ValueError('independent initial clock did not resume')
    refresh=[r for r in rows if r.get('event')=='duration-refresh']
    if len(refresh)!=1 or refresh[0]['scene']!=3 or refresh[0]['vtable']!='b0aa60' or refresh[0]['duration']!=0x40400000 or refresh[0]['before']['pauseCount']!=1:raise ValueError('missing live paused-owner refresh')
    if [r['word']for r in rows if r.get('event')=='sleep-main-duration']!=[0x40400000]*4:raise ValueError('actual main duration differs')
    if [r['duration']for r in rows if r.get('event')=='sleep-pause-source']!=[0x40000000]*5:raise ValueError('actual initial duration differs')
    if sum(r.get('event')=='sleep-main-duration'for r in rows)!=4:raise ValueError('refresh replaced main owner')

def semantic(rows,index):
    if index==4:return refresh_semantic(rows)
    if index==2:return nested_semantic(rows)
    if index==3:
        for scene in range(8):
            selected=[r for r in rows if r.get('scene')==scene]
            expected='adcec8'if scene<4 else'b0a6e4'
            pause=[r['vtable']for r in selected if r.get('event')=='pause-initialize']
            if pause!=[expected]:raise ValueError('authored alias changed concrete pause class')
            sleep=[r['vtable']for r in selected if r.get('event')=='sleep-initialize']
            if sleep!=([]if scene<4 else['b0aa60']):raise ValueError('missing concrete Sleep owner')
        if any('factory'in r.get('event','')for r in rows):raise ValueError('unexpected Stun factory in authored-alias scenes')
        return
    marks=[r['value']for r in rows if r.get('event')=='marker']
    if index==0:
        adds=[m for m in marks if 'label=add-after 'in m]
        if len(adds)!=7 or any('result=0 level=0'not in m for m in adds[:6])or 'result=1 level=1'not in adds[6]:raise ValueError('stock attachment controls differ')
        return
    for scene in range(3):
        def mark(label):
            found=[m for m in marks if 'scene='+str(scene)+' 'in m and 'label='+label+' 'in m]
            if len(found)!=1:raise ValueError('missing semantic marker '+label)
            return found[0]
        early=mark('damage-after')
        if 'sleep=1 pause=1 stun=0 order=851973 life=420.0000'not in early:raise ValueError('initial damage guard differs')
        if 'sleep=1 pause=1 stun=0 order=851973'not in mark('move-after')or 'result=1'not in mark('move-after'):raise ValueError('held Move rejected')
        if scene==1:
            if 'sleep=1 pause=0 stun=0 order=851973'not in mark('damage-late-after')or 'life=420.0000'in mark('damage-late-after'):raise ValueError('late damage suppressed')
            if 'sleep=0 pause=0 stun=0 order=851986'not in mark('move-late-before'):raise ValueError('late wake did not resume held Move')
        elif scene==2:
            if 'sleep=0 pause=0 stun=0'not in mark('remove-after'):raise ValueError('script inverse missing')
        else:
            if 'sleep=1 pause=0 stun=0 order=851973'not in mark('move-late-after'):raise ValueError('guard expiry removed Sleep')
    for event in ('sleep-apply','sleep-pause-initialize','sleep-pause-remove'):
        if sum(r.get('event')==event for r in rows)!=3:raise ValueError('missing concrete Sleep owner')
    for r in rows:
        for key in ('before','after'):
            state=r.get(key)
            if isinstance(state,dict)and('execution'in state)and(state['execution']or state['orders']):raise ValueError('Sleep acquired scripted ability disables')
def runtime(experiments,spec):
    if len(experiments)!=5 or len(spec.get('experiments',[]))!=5:raise ValueError('missing experiment')
    for index,(experiment,contract)in enumerate(zip(experiments,spec['experiments'])):
        cases=experiment['cases']
        if len(cases)!=3:raise ValueError('missing repeats/control')
        for i,case in enumerate(cases):
            rows=case['rows'];meta=rows[0];marks=public(case['preload'],contract['prefix'])
            if(meta.get('task')!='GROUP-03.2'or meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('mode')!=('control'if i==2 else'observe')or meta.get('launcher')!='owned-wine-popen'or meta.get('env')not in('B','C')or meta.get('source_sha256')!=contract['capture_sources'][i]or meta['source_sha256']['map']!=contract['map_sha256']or any(r.get('type')=='error'or r.get('event')in('compile-error','trace-failed')for r in rows)):raise ValueError('capture provenance/failure')
            if marks!=contract['public']or len(marks)!=[51,45,82,58,96][index]or rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':len(marks),'complete':True}:raise ValueError('incomplete public stream')
            if i==2:
                if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
                continue
            end=[r for r in rows if r.get('event')=='trace-end']
            if len(end)!=1 or not end[0].get('installed')or not end[0].get('readOnly')or end[0]['depth']or end[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r)):raise ValueError('unfinished observer')
            if projection(rows)!=contract['events']or [r['value']for r in rows if r.get('event')=='marker']!=marks:raise ValueError('native chronology differs')
            semantic(rows,index)
        if digest(base64.b64decode(experiment['map_input']))!=contract['base_sha256']:raise ValueError('unknown map base')
    return dict(captures=15,scenes=26,public_markers=332,native_events=622)
def validate(spec):
    if spec.get('version')!=1 or spec.get('task')!='GROUP-03.2'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=2246 or spec.get('functions')!=63 or spec.get('data_guards')!={'6fcd539c':'17b7d138'} or [len(e['events'])for e in spec.get('experiments',[])]!=[51,71,186,82,232]:raise ValueError('incomplete contract')
    for p,h in spec['pins'].items():
        if digest((ROOT/p).read_bytes())!=h:raise ValueError('source changed '+p)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle changed')
    experiments=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(experiments,spec);return experiments

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());experiments=validate(spec);original_bytes(a.binary,spec['instructions']);original_bytes(a.binary,spec['data_guards'])
    with tempfile.TemporaryDirectory(prefix='group367-')as tmp:
        for i,(e,c)in enumerate(zip(experiments,spec['experiments'])):
            base=Path(tmp)/('base'+str(i)+'.w3m');base.write_bytes(base64.b64decode(e['map_input']));out=Path(tmp)/('out'+str(i)+'.w3m')
            command=[sys.executable,str(ROOT/c.get('builder','tools/frida/research/group341_make_map.py')),'--base',str(base),'--output',str(out)]
            if 'builder'not in c:command+=['--probe',str(ROOT/c['probe'])]
            subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
            if digest(out.read_bytes())!=c['map_sha256']:raise ValueError('map reconstruction differs')
    from wc3_pathfinding_engine_tests import run_engine
    engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=2246,functions=63,data_guards=1,**runtime(experiments,spec),engine=engine);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
