"""Read-only original coupling witness audit; does not modify frozen inputs."""
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import base64
import gzip

ROOT=Path(__file__).resolve().parents[3]
FIXTURES=ROOT/'tools/ghidra/fixtures'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
MAP_SHA='f373cd766271b876768857c263b850f6d7b80ba303274dbc38fa6236de3bd3ac'
SOURCE_PINS={'itemdrop_capture.py':'f117909276295c4ba3d7832f0883f2a5e565c73a7c5eab30aa55cb752aa6b13e','itemdrop_observer.js':'d0d3df02ddb4c4f6c03be16a9ac146d48d9341ed4a2d446fdd4ceafaea778e9a','itemdrop_make_map.py':'7909f2376e753f4c3d6e6bf7624c00b6564c57b73a9369d160d906a1cb2e7b57','itemdrop_probe.j':'64d05dffaa602a64c4b301f23b6d7eb031bad641287d0a742656f98a73f7c2a9'}
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
RANGE=0x43a68000
ARCHER=int.from_bytes(b'earc','big');HIPPO=int.from_bytes(b'ehip','big')

def need(value,message):
    if not value:raise ValueError(message)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def audit_accepted(data):
    need(sha(data['map'])==MAP_SHA,'map digest')
    for name,pin in SOURCE_PINS.items():
        need(sha(data['sources'][name])==pin and data['freeze'][name]==pin,'source/freeze digest')
    need(data['freeze']['TargetCoupling177.w3m']==MAP_SHA,'frozen map digest')
    # Exact two original-object rank-one real aran fields; no other ability changes.
    expected=struct.pack('<II',2,2)
    for raw in (b'Acoa',b'Acoh'):
        expected+=raw+b'\0'*4+struct.pack('<I',1)+b'aran'+struct.pack('<IIII',2,1,0,RANGE)+b'\0'*4
    expected+=struct.pack('<I',0)
    need(data['w3a']==expected,'decoded w3a overrides')
    need(len(data['captures'])==3,'capture cardinality')
    streams=[];projections=[];row_counts=[]
    for index,capture in enumerate(data['captures']):
        rows=capture['rows'];preload=capture['preload'];header=rows[0];env=header['env']
        need(header['event']=='metadata' and header['owned'] is True and header['sha256']==SHA and header['mode']==('control' if index==2 else 'observe'),'metadata ownership/binary/mode')
        need(header['task']=='TARGET-01.2-militia' and header['prefix']=='COUP ' and header['map']=='Maps/TargetCoupling177.w3m','preserved inherited label/coupling scope')
        need(env in ('B','C') and header['display']=={'B':':98','C':':99'}[env] and header['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'owned environment')
        need(header['source_sha256']==dict(SOURCE_PINS,map=MAP_SHA),'capture source pins')
        need(not any(r.get('event') in ('trace-failed','error','script-error') or r.get('type')=='error' for r in rows),'observer errors')
        markers=re.findall(r'call Preload\( "(COUP [^"\r\n]*)" \)',preload.decode())
        need(len(markers)==333 and markers[-1]=='COUP tick=1999 scene=4 label=complete','public completion')
        need(rows[-1]==dict(event='preload-file',sha256=sha(preload),markers=333,complete=True),'preload footer')
        parsed=[dict(t.split('=',1) for t in m.split()[1:]) for m in markers]
        for scene in range(4):
            p=[r for r in parsed if r['scene']==str(scene)]
            issue=[r for r in p if r['label']=='order-return'];end=[r for r in p if r['label']=='settled']
            need(len(issue)==len(end)==1,'public label cardinality');issue=issue[0];end=end[0]
            need(issue['accepted']==issue['aa']==issue['ha']==issue['reht']=='1' and issue['range']=='333','public-added admission')
            need((issue['ah'],issue['hh'])==(('852143','852141') if scene%2==0 else ('852141','852142')),'public reciprocal heads')
            need(any(float(r['ax'])>float(issue['ax'])+10 and float(r['hx'])<float(issue['hx'])-10 and r['riders']=='0' for r in p),'both actors approach')
            need(end['riders']=='1' and end['at']==end['ht']=='0','public merge completion')
        if index==2:need(all(r['event'] in CONTROL and 'seq' not in r for r in rows),'unhooked control')
        else:
            need([r['value'] for r in rows if r['event']=='marker']==['COUP tick=0 scene=-1 label=start']+markers,'observer public equality')
            ends=[r for r in rows if r['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'observer footer')
            counts=ends[0]['counts'];need(Counter(r['event'] for r in rows if r['event'] in counts)==Counter(counts),'trace counts')
            need({r['event'] for r in rows}-CONTROL-{'trace-end'}==set(counts),'trace event inventory')
            projection=[]
            for scene in range(4):
                rs=[r for r in rows if r.get('scene')==scene]
                prods=[r for r in rs if r['event']=='couple-producer-enter'];tasks=[r for r in rs if r['event']=='target-task'];ready=[r for r in rs if r['event']=='couple-ready'];merge=[r for r in rs if r['event']=='couple-merge']
                need(len(prods)==2 and [r['order'] for r in prods]==[852143 if scene%2==0 else 852142,852141],'reciprocal producers')
                actors={r['actor']['pointer']:r['actor'] for r in prods}
                need(len(actors)==2 and {r['rawcode'] for r in actors.values()}=={ARCHER,HIPPO},'distinct actor types')
                need([r['actor']['rawcode'] for r in prods]==([ARCHER,HIPPO] if scene%2==0 else [HIPPO,ARCHER]),'actor/order attribution')
                need(len(tasks)==4 and [r['code'] for r in tasks]==[0xd018a,0xd0171]*2,'task codes')
                for i,t in enumerate(tasks):
                    source=prods[i//2]['actor'];target=prods[1-i//2]['actor']
                    need(t['unit']['pointer']==source['pointer'] and t['unit']['identity']==source['identity'] and t['unit']['rawcode']==source['rawcode'],'task actor incarnation')
                    need(t['target']==target['pointer'],'reciprocal task target')
                    need(t['range']==0 and t['caller']==[0x4e3d55,0x4e3d69][i%2],'task range/caller')
                need(len(ready)==2 and len(merge)==1 and {r['actor']['pointer'] for r in ready}==set(actors),'two distinct ready owners')
                for r in ready+merge:
                    need(r['actor']['pointer'] in actors,'completion actor')
                    a=actors[r['actor']['pointer']];other=next(v for k,v in actors.items() if k!=a['pointer'])
                    need(r['actor']['identity']==a['identity'] and r['partner']==other['identity'],'completion reciprocal incarnation')
                    need(r['range']==RANGE,'authored completion333')
                need(max(rs.index(r) for r in ready)<rs.index(merge[0]),'ready before merge')
                projection.append({'scene':scene,'orders':[r['order'] for r in prods],'ready_types':[r['actor']['rawcode'] for r in ready],'ready_last_marker_ticks':[r['tick'] for r in ready],'merge_type':merge[0]['actor']['rawcode'],'merge_last_marker_tick':merge[0]['tick']})
            projections.append(projection)
        streams.append(markers);row_counts.append(len(rows))
    need(streams[0]==streams[1]==streams[2],'exact public repeat/control')
    need(projections[0]==projections[1],'normalized producer repeat')
    return {'passed':True,'observations':2,'controls':1,'markers_per_capture':333,'matching_public_markers':999,'rows':row_counts,'source_sha256':dict(SOURCE_PINS,map=MAP_SHA),'public_projection_sha256':sha('\n'.join(streams[0]).encode()),'engine_parity':False,'remaining_owner':'TARGET-01.2','scope':'Explicit public-added Acoa/Acoh, Reht1, rank1 authored333; not natural patched stock Acoi availability.','metadata_caveat':'Inherited task label TARGET-01.2-militia is preserved; COUP prefix, exact map and source pins identify coupling.','time_caveat':'Hook ticks are last public marker ticks; raw clock counters are preserved, not claimed as identical absolute start clocks.','projection':projections[0]}

def mutation_checks(data):
    checks=[]
    def reject(name,edit):
        d=copy.deepcopy(data);edit(d)
        try:audit_accepted(d)
        except ValueError as e:checks.append({'name':name,'rejected':str(e)});return
        raise AssertionError('mutation accepted: '+name)
    def event(d,name):return next(r for r in d['captures'][0]['rows'] if r['event']==name)
    reject('task opposing target',lambda d:event(d,'target-task').update(target='0x0'))
    reject('task incarnation',lambda d:event(d,'target-task')['unit'].update(identity=[0,0]))
    reject('literal zero range',lambda d:event(d,'target-task').update(range=RANGE))
    reject('task caller',lambda d:event(d,'target-task').update(caller=0))
    reject('ready partner incarnation',lambda d:event(d,'couple-ready').update(partner=[0,0]))
    reject('authored333',lambda d:event(d,'couple-ready').update(range=0))
    reject('trace count',lambda d:event(d,'trace-end')['counts'].update(marker=1))
    reject('unhookedcontrol',lambda d:d['captures'][2]['rows'].insert(1,{'event':'velocity'}))
    reject('map override',lambda d:d.update(w3a=d['w3a'][:-1]))
    reject('frozen source',lambda d:d['sources'].update({'itemdrop_probe.j':b'changed'}))
    reject('ownership',lambda d:d['captures'][0]['rows'][0].update(owned=False))
    def reorder(d):
        rows=d['captures'][0]['rows'];a=next(i for i,r in enumerate(rows) if r['event']=='couple-merge');b=next(i for i,r in enumerate(rows) if r['event']=='couple-ready');r=rows.pop(a);rows.insert(b,r)
    reject('ready-before-merge',reorder)
    return checks



BUNDLE=FIXTURES/'retail-coupling-producer-1.27.json.gz'
SPEC=json.loads((FIXTURES/'retail-coupling-producer-1.27.json').read_text())

def literal(obj,prefix='raw'):
    raw=base64.b64decode(obj[prefix+'_b64'],validate=True)
    need(sha(raw)==obj[prefix+'_sha256'],'literal digest')
    return raw

def unpack(sample,key):
    pin=SPEC['pins'][key]
    need(sha(literal(sample['map']))==pin['map'],'pinned map')
    need(set(sample['sources'])==set(pin['sources']),'source inventory')
    sources={}
    for n,s in sample['sources'].items():
        sources[n]=literal(s)
        need(sha(sources[n])==pin['sources'][n],'pinned source')
        need((FIXTURES/'research'/('coupling-producer' if key=='accepted' else 'coupling-rejected-setup')/n).read_bytes()==sources[n],'readable source')
    need(len(sample['captures'])==3,'capture cardinality')
    captures=[]
    for name,c in zip(('observe1','observe2','control1'),sample['captures']):
        captures.append({'name':name,'rows':[json.loads(l) for l in literal(c).splitlines()],'preload':literal(c,'preload')})
    return {'map':literal(sample['map']),'sources':sources,'captures':captures}

def audit_rejected(data):
    pin=SPEC['pins']['rejected_missing_abilities'];streams=[]
    for i,c in enumerate(data['captures']):
        rows=c['rows'];h=rows[0];raw=c['preload'];env=h['env']
        need(h['event']=='metadata' and h['owned'] is True and h['sha256']==SHA and h['mode']==('control' if i==2 else 'observe'),'rejected provenance')
        need(h['task']==SPEC['metadata_task'] and h['prefix']=='COUP ','rejected metadata label')
        need(env in ('B','C') and h['display']=={'B':':98','C':':99'}[env] and h['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'rejected environment')
        need(h['source_sha256']==dict(pin['sources'],map=pin['map']),'rejected source pins')
        need(not any(r.get('event') in ('trace-failed','error','script-error') or r.get('type')=='error' for r in rows),'rejected observer errors')
        markers=re.findall(r'call Preload\( "(COUP [^"\r\n]*)" \)',raw.decode())
        need(len(markers)==333 and markers[-1]=='COUP tick=1999 scene=4 label=complete','rejected completion')
        need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=333,complete=True),'rejected footer')
        parsed=[dict(t.split('=',1) for t in m.split()[1:]) for m in markers]
        issues=[r for r in parsed if r['label']=='order-return'];ends=[r for r in parsed if r['label']=='settled']
        need(len(issues)==len(ends)==4 and all(r['accepted']==r['aa']==r['ha']=='0' and r['reht']=='1' and r['ah']==r['hh']=='0' for r in issues),'missing-ability rejection')
        need(all(r['riders']=='0' for r in ends),'rejected merge')
        if i<2:
            need([r['value'] for r in rows if r['event']=='marker']==['COUP tick=0 scene=-1 label=start']+markers,'rejected observer/public')
            footer=[r for r in rows if r['event']=='trace-end'];need(len(footer)==1 and footer[0]['installed'] is True,'rejected observer footer')
            counts=footer[0]['counts'];need(Counter(r['event'] for r in rows if r['event'] in counts)==Counter(counts),'rejected trace counts')
            need({r['event'] for r in rows}-CONTROL-{'trace-end'}==set(counts),'rejected event inventory')
            need(not any(r['event'] in ('couple-producer-enter','target-task','couple-merge') for r in rows),'rejected setup reached producer')
        else:need(all(r['event'] in CONTROL and 'seq' not in r for r in rows),'rejected control hooked')
        streams.append(markers)
    need(streams[0]==streams[1]==streams[2],'rejected public repeats')
    need(sha('\n'.join(streams[0]).encode())==pin['public'],'rejected public projection')
    return 999

def audit(bundle=None):
    if bundle is None:
        raw=BUNDLE.read_bytes();need(sha(raw)==SPEC['bundle_sha256'],'compressed bundle digest')
        bundle=json.loads(gzip.decompress(raw))
    b=bundle
    need(b['version']==SPEC['version']==1 and b['task']==SPEC['task']=='TARGET-01.2' and b['engine_parity'] is SPEC['engine_parity'] is False,'evidence scope')
    accepted=unpack(b['accepted'],'accepted');accepted['freeze']=b['accepted']['freeze'];accepted['w3a']=literal(b['accepted']['w3a'])
    result=audit_accepted(accepted)
    need(result['public_projection_sha256']==SPEC['pins']['accepted']['public'],'accepted public projection')
    rejected=audit_rejected(unpack(b['rejected_missing_abilities'],'rejected_missing_abilities'))
    return {'observations':2,'controls':1,'public_markers':999,'rejected_missing_ability_markers':rejected,'engine_parity':False,'remaining_owner':'TARGET-01.2'}
