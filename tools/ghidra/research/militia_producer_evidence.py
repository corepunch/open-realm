"""Frozen Militia producer witnesses; engine parity and wider timing are unclaimed."""
import base64
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[3]
FIXTURES=ROOT/'tools/ghidra/fixtures'
BUNDLE=FIXTURES/'retail-militia-producer-1.27.json.gz'
SPEC=json.loads((FIXTURES/'retail-militia-producer-1.27.json').read_text())
PINS=SPEC['pins']
PUBLIC=SPEC['public_digests']
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
RANGE=0x43a68000
MAX=0x7f7fffff

def require(ok,message):
    if not ok: raise ValueError(message)

def digest(raw): return hashlib.sha256(raw).hexdigest()

def literal(obj,prefix='raw'):
    raw=base64.b64decode(obj[prefix+'_b64'],validate=True)
    require(digest(raw)==obj[prefix+'_sha256'],'literal hash differs')
    return raw

def projection(rows):
    out=[]
    for scene in range(6):
        selected=[r for r in rows if r.get('scene')==scene and r.get('tick')==scene*500+100]
        def events(name,caller=None):
            return [r for r in selected if r['event']==name and (caller is None or r.get('caller')==caller)]
        producer=events('militia-producer-enter')
        require(len(producer)==1,'producer cardinality differs')
        require(producer[0]['order']==(852071 if scene<3 else 852651) and producer[0]['vtable']==0xb701e0,'producer order/class differs')
        authored=events('militia-authored-range')
        require(len(authored)==1 and authored[0]['value']==RANGE and authored[0]['rank']==0,'authored range differs')
        ranges=events('target-range-query',0x62e985)
        kind=scene%3
        require(len(ranges)==1 and ranges[0]['range']==RANGE and ranges[0]['result']==(kind!=0),'range decision differs')
        facing=events('facing-query',0x62e9b7)
        require(len(facing)==(kind!=0),'facing cardinality differs')
        if facing: require(facing[0]['window']==0x3e4ccccd and facing[0]['result']==(kind==1),'facing decision differs')
        tasks=events('target-task',0x62e9ce)
        require(len(tasks)==(kind!=1),'physical task cardinality differs')
        if tasks: require(tasks[0]['code']==0xd0174 and tasks[0]['range']==(RANGE if kind==0 else MAX),'physical task/range differs')
        out.append((scene,producer[0]['order'],ranges[0]['result'],None if not facing else facing[0]['result'],None if not tasks else tasks[0]['range']))
    return out

def audit(bundle=None):
    require(SPEC['version']==1 and SPEC['task']=='TARGET-01.2' and SPEC['engine_parity'] is False,'wrong fixture spec')
    data=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version']==1 and data['task']=='TARGET-01.2' and data['binary_sha256']==SHA and data['engine_parity'] is False,'wrong evidence contract')
    for kind in ('accepted','rejected_wrong_names'):
        sample=data[kind];pins=PINS[kind]
        require(digest(literal(sample['map']))==pins['map'],'map pin differs')
        require(set(sample['sources'])==set(pins['sources']),'source inventory differs')
        for name,source in sample['sources'].items():
            require(digest(literal(source))==pins['sources'][name],'source pin differs')
            directory='militia-producer' if kind=='accepted' else 'militia-rejected-names'
            require((FIXTURES/'research'/directory/name).read_bytes()==literal(source),'readable source differs')
        require(len(sample['captures'])==3,'two observations and control required')
        streams=[];projections=[]
        for index,capture in enumerate(sample['captures']):
            rows=[json.loads(line) for line in literal(capture).splitlines()]
            preload=literal(capture,'preload');header=rows[0];footer=rows[-1]
            mode='control' if index==2 else 'observe';env=header['env']
            require(header['event']=='metadata' and header['mode']==mode and header['owned'] is True and header['sha256']==SHA and header['task']=='TARGET-01.2-militia' and header['prefix']=='MIL ','capture provenance differs')
            require(env in ('B','C') and header['display']=={'B':':98','C':':99'}[env] and header['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'capture environment differs')
            require(header['source_sha256']==dict(pins['sources'],map=pins['map']),'capture source pins differ')
            require(not any(r.get('event') in ('trace-failed','error','script-error') or r.get('type')=='error' for r in rows),'failed observer')
            markers=re.findall(r'call Preload\( "(MIL [^"\r\n]*)" \)',preload.decode())
            require(len(markers)==508 and markers[-1]=='MIL tick=2999 scene=6 label=complete','public completion differs')
            require(footer==dict(event='preload-file',sha256=digest(preload),markers=508,complete=True),'preload footer differs')
            public=[dict(token.split('=',1) for token in value.split()[1:]) for value in markers]
            issues=[r for r in public if r['label']=='order-return']
            require(len(issues)==6 and all(r['accepted']==('1' if kind=='accepted' else '0') for r in issues),'accepted/rejected public orders differ')
            if mode=='observe':
                require([r['value'] for r in rows if r.get('event')=='marker']==['MIL tick=0 scene=-1 label=start']+markers,'observer/public stream differs')
                ends=[r for r in rows if r.get('event')=='trace-end']
                require(len(ends)==1 and ends[0]['installed'] is True,'observer completion differs')
                counts=ends[0]['counts']
                actual=Counter(r['event'] for r in rows if r['event'] in counts)
                require(actual==Counter(counts),'observer counts differ')
                hook_events={r['event'] for r in rows}-CONTROL-{'trace-end'}
                require(hook_events==set(counts),'observer event inventory differs')
                if kind=='accepted': projections.append(projection(rows))
                else:require(not any(r['event']=='militia-producer-enter' for r in rows),'wrong names reached producer')
            else:require(all(r['event'] in CONTROL and 'seq' not in r for r in rows),'control contains observer hooks')
            streams.append(markers)
        require(streams[0]==streams[1]==streams[2],'public repeats/control differ')
        require(digest('\n'.join(streams[0]).encode())==PUBLIC[kind],'literal public projection differs')
        if kind=='accepted': require(projections[0]==projections[1],'producer repeats differ')
    return dict(observations=2,controls=1,public_markers=1524,rejected_wrong_name_markers=1524,engine_parity=False,remaining_owner='TARGET-01.2')

if __name__=='__main__':print(json.dumps(audit(),indent=2))
