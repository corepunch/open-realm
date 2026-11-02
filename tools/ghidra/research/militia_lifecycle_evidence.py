"""Native nine-scene Militia lifecycle; no engine parity or missing-event inference."""
from collections import Counter
import gzip,json,re
from militia_producer_evidence import ROOT,FIXTURES,SHA,CONTROL,RANGE,MAX,require,digest,literal
BUNDLE=FIXTURES/'retail-militia-lifecycle-1.27.json.gz'
SPEC=json.loads((FIXTURES/'retail-militia-lifecycle-1.27.json').read_text())
PINS=SPEC['pins']

def public_contract(markers):
    require(len(markers)==490 and markers[-1]=='MIL tick=3149 scene=9 label=complete','public completion differs')
    parsed=[dict(token.split('=',1) for token in value.split()[1:]) for value in markers]
    xy=lambda r:(r['x'],r['y'])
    for scene in range(9):
        rows=[r for r in parsed if r['scene']==str(scene)]
        def get(label):
            found=[r for r in rows if r['label']==label]
            require(len(found)==1,'public label cardinality differs: '+label)
            return found[0]
        issue=get('order-return');final=get('settled');before=get('before-interrupt')
        require(issue['accepted']=='1' and issue['head']=='852071','public admission differs')
        require(final['head']=='0' and final['bmil']==('1' if scene in (0,6) else '0'),'final conversion differs')
        if scene in (1,3,7):require(xy(final)==xy(before),'stopped position differs')
        if scene in (1,7):require(get('stop-return')['accepted']=='1' and get('stop-return')['head']=='0','Stop ownership differs')
        if scene in (2,8):
            move=get('move-return')
            require(move['accepted']=='1' and move['head']=='851986' and float(final['x'])<550 and float(final['y'])<810,'replacement Move differs')
        if scene in (4,5):
            after=get('remove-amic' if scene==4 else 'remove-amil')
            require(after['accepted']=='1' and after['head']=='852071','ability-loss admission differs')
            require(any(r['label']=='sample' and r['head']=='852071' and float(r['x'])>float(after['x'])+50 for r in rows),'ability-loss retained motion differs')
            require(float(final['x'])>1000,'ability-loss completion differs')
        if scene==3:
            removed=get('remove-hall')
            require(removed['head']=='852071','Hall removal synchronous head differs')
            early=next(r for r in rows if r['label']=='sample' and int(r['tick'])>int(removed['tick']))
            require(early['head']=='0' and xy(early)==xy(before),'Hall removal early stop differs')
        if scene==6:
            require(xy(get('pause'))==xy(get('before-resume'))==xy(get('resume')),'pause position differs')
            require(get('pause')['head']=='851973','pause owner differs')

def projection(rows):
    result=[]
    for scene in range(9):
        part=[r for r in rows if r.get('scene')==scene and r.get('tick')==scene*350+100]
        producers=[r for r in part if r['event']=='militia-producer-enter']
        tasks=[r for r in part if r['event']=='target-task' and r.get('caller')==0x62e9ce]
        require(len(producers)==1 and producers[0]['order']==852071 and producers[0]['vtable']==0xb701e0,'producer differs')
        require(len(tasks)==1 and tasks[0]['code']==0xd0174 and tasks[0]['range']==(RANGE if scene<7 else MAX),'physical owner differs')
        result.append((scene,producers[0]['order'],tasks[0]['range']))
    return result

def audit(bundle=None):
    require(SPEC['version']==1 and SPEC['task']=='TARGET-01.2' and SPEC['engine_parity'] is False,'wrong fixture spec')
    d=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(d['version']==1 and d['task']=='TARGET-01.2' and d['binary_sha256']==SHA and d['engine_parity'] is False,'wrong lifecycle contract')
    require(digest(literal(d['map']))==PINS['map'],'map pin differs')
    require(set(d['sources'])==set(PINS['sources']),'source inventory differs')
    for n,s in d['sources'].items():
        require(digest(literal(s))==PINS['sources'][n],'source pin differs')
        require((FIXTURES/'research/militia-lifecycle'/n).read_bytes()==literal(s),'readable source differs')
    require(len(d['captures'])==3,'two observations and control required')
    streams=[];projections=[]
    for i,c in enumerate(d['captures']):
        rows=[json.loads(line) for line in literal(c).splitlines()];preload=literal(c,'preload');h=rows[0];env=h['env']
        require(h['event']=='metadata' and h['mode']==('control' if i==2 else 'observe') and h['owned'] is True and h['sha256']==SHA and h['task']=='TARGET-01.2-militia' and h['prefix']=='MIL ','capture provenance differs')
        require(env in ('B','C') and h['display']=={'B':':98','C':':99'}[env] and h['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'capture environment differs')
        require(h['source_sha256']==dict(PINS['sources'],map=PINS['map']),'capture source pins differ')
        require(not any(r.get('event') in ('trace-failed','error','script-error') or r.get('type')=='error' for r in rows),'failed observer')
        markers=re.findall(r'call Preload\( "(MIL [^"\r\n]*)" \)',preload.decode());public_contract(markers)
        require(rows[-1]==dict(event='preload-file',sha256=digest(preload),markers=490,complete=True),'preload footer differs')
        if i<2:
            require([r['value'] for r in rows if r['event']=='marker']==['MIL tick=0 scene=-1 label=start']+markers,'observer/public differs')
            ends=[r for r in rows if r['event']=='trace-end'];require(len(ends)==1 and ends[0]['installed'] is True,'observer completion differs')
            counts=ends[0]['counts'];require(Counter(r['event'] for r in rows if r['event'] in counts)==Counter(counts),'observer counts differ')
            require({r['event'] for r in rows}-CONTROL-{'trace-end'}==set(counts),'observer inventory differs')
            projections.append(projection(rows))
        else:require(all(r['event'] in CONTROL and 'seq' not in r for r in rows),'control contains hooks')
        streams.append(markers)
    require(streams[0]==streams[1]==streams[2],'public repeats/control differ')
    require(digest('\n'.join(streams[0]).encode())==PINS['public'],'literal public projection differs')
    require(projections[0]==projections[1],'producer repeats differ')
    return dict(observations=2,controls=1,public_markers=1470,scenes=9,engine_parity=False,remaining_owner='TARGET-01.2')
if __name__=='__main__':print(json.dumps(audit(),indent=2))
