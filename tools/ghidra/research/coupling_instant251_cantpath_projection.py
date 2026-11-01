#!/usr/bin/env python3
"""Retail251 terminal cannot-path ownership; offline strict replay and header audit."""
import pathlib, json, re, hashlib, collections, copy
import coupling_instant243_preparation_projection as m
from coupling_instant243_preparation_projection import require
ROOT = pathlib.Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_couple_instant251_cantpath.json'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def rows(p):
    return [json.loads(x) for x in p.read_text().splitlines()]

def marks(p):
    return re.findall('call Preload\\( "(CPF .*?)" \\)', p.read_text())

POLL_INDEX={0:[2,3,4,5,6,7,8,18,10,11],2:[2,13,14,15,16,7]}
def project_polls(path):
 identities={};raw=[];typed=[]
 for x in rows(path):
  if x.get('event') not in ['arrival','velocity']:continue
  key=(x['scene'],x['before']['mover'])
  if key not in identities:
   pose=x['before']['pose'];xy=pose[2:4]
   require(xy[1]==0x41800000,'initial poll Y')
   if pose[6]==0x41480000:role=1;require(xy[0]==0x42000000 and x['before']['radius']==0x3e800000,'Hippo initial pose/radius')
   else:
    require(pose[6]==0x41070000 and x['before']['radius']==0x3f800000,'Archer profile')
    require(xy[0] in [0x41800000,0x42098000],'initial Archer pose')
    role=0 if xy[0]==0x41800000 else 2
   require((x['scene'],role) not in identities.values(),'unambiguous actor role')
   identities[key]=(x['scene'],role)
  role=identities[key][1];y=copy.deepcopy(x)
  for part in ['before','after']:
   if part in y:
    require(y[part]['mover']==key[1],'same actual mover entry/leave');y[part]['mover']={'actor_role':role}
  raw.append(y)
  if x['event']=='arrival':phase=0;words=[x['clock']['primary'][0],*x['source'],*x['destination'],x['heading'],x['threshold'],x['angle'],x['inside'],x['result']]
  else:phase=2;words=[x['clock']['primary'][0],*x['after']['pose'][2:6],x['after']['heading']]
  require(all(type(w) is int and 0<=w<=0xffffffff for w in words),'raw poll uint32')
  typed.append(dict(scene=x['scene'],phase=phase,role=role,words=words))
 require(len(raw)==160 and collections.Counter(x['event'] for x in raw)=={'arrival':80,'velocity':80},'complete160polls')
 return raw,typed

def poll_header(d):
 s='/* role0 originalArcher(userdata1),1 Hippo(2),2 freshArcher(3); phase1 not captured. */\n'
 s+='typedef struct { unsigned scene,phase,role,count; uint32_t words[10]; } coupleNativePoll251_t;\nstatic coupleNativePoll251_t const couple_native_polls251[] = {\n'
 for x in d['typed_polls']:s+='{%d,%d,%d,%d,{%s}},\n'%(x['scene'],x['phase'],x['role'],len(x['words']),','.join('0x%08xu'%v for v in x['words']))
 return s+'};\n'

def project(capture_dir):
    R = pathlib.Path(capture_dir)
    f = json.loads((R / 'frozen-sources.json').read_text())
    require(all((sha(R / n) == h for n, h in f.items() if n != 'original_dll')), 'native251 invariant')
    require(all((x['exit'] == 0 for x in json.loads((R / 'runs.json').read_text()))), 'native251 invariant')
    public = []
    native = []
    summaries = []
    for n in ['observe1', 'observe2', 'control1']:
        a = rows(R / (n + '.jsonl'))
        meta = a[0]
        env = meta['env']
        require(env in ['B', 'C'] and meta['owned'], 'native251 invariant')
        require(meta['display'] == {'B': ':98', 'C': ':99'}[env] and meta['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env], 'native251 invariant')
        require(meta['sha256'] == f['original_dll'] == 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236', 'native251 invariant')
        require(meta['source_sha256'] == {**{k: f[k] for k in ['itemdrop_capture.py', 'itemdrop_make_map.py', 'itemdrop_probe.j', 'itemdrop_observer.js']}, 'map': f['RS-TARGET251-couple-cantpath-v1.w3m']}, 'native251 invariant')
        ms = marks(R / (n + '-preload.txt'))
        public.append(ms)
        require(ms and max(map(len, ms)) <= 160 and (not any(('OVERLONG' in x for x in ms))), 'native251 invariant')
        require(a[-1] == {'event': 'preload-file', 'sha256': sha(R / (n + '-preload.txt')), 'markers': len(ms), 'complete': True}, 'native251 invariant')
        require(not any((x.get('event') in ['error', 'trace-failed', 'script-error'] or x.get('type') == 'error' for x in a)), 'native251 invariant')
        if n == 'control1':
            require(all((x['event'] in ['metadata', 'owned-loading-input', 'loading-key', 'control-start-file', 'preload-file'] for x in a)), 'native251 invariant')
            continue
        ends = [x for x in a if x['event'] == 'trace-end']
        require(len(ends) == 1 and ends[0]['installed'], 'native251 invariant')
        c = ends[0]['counts']
        require(collections.Counter((x['event'] for x in a if x['event'] in c)) == collections.Counter(c), 'native251 invariant')
        hs = [x['value'] for x in a if x['event'] == 'marker']
        require(hs[-len(ms):] == ms, 'native251 invariant')
        out = []
        for x in a:
            e = x['event']
            p = m.projection([x])
            if p:
                out += p
            elif e in ['instant-inverse', 'instant-coordinate', 'instant-terminal', 'instant-dispatch']:
                b = x['ability']
                y = {k: x[k] for k in ['event', 'tick', 'scene', 'clock', 'caller', 'code'] if k in x}
                y['ability'] = {k: b[k] for k in ['identity', 'alias', 'flags', 'counters', 'rank', 'data', 'retained']}
                y['ability']['actor'] = m.actor(b['actor'])
                y['target'] = m.actor(x['target']) if x.get('target') else None
                out.append(y)
            elif e in ['cant-path', 'blocked', 'unit-completion-event']:
                y = {k: x[k] for k in ['event', 'tick', 'scene', 'clock', 'partial', 'identity', 'code'] if k in x}
                if 'actor' in x:
                    y['actor'] = m.actor(x['actor']) if x['actor'] else None
                out.append(y)
            elif e.startswith('reservation-'):
                y = {k: x[k] for k in ['event', 'tick', 'scene', 'clock', 'tag', 'player', 'caller'] if k in x}
                if x.get('marker'):
                    y['marker'] = {k: x['marker'][k] for k in ['identity', 'tag', 'player', 'refs']}
                    y['target'] = m.actor(x['marker']['target'])
                elif x.get('target') and isinstance(x['target'], dict):
                    y['target'] = m.actor(x['target'])
                out.append(y)
        native.append(out)
        for scene in [0, 1]:
            failures = [x for x in a if x['event'] == 'cant-path' and x['scene'] == scene and (x['tick'] < scene * 1000 + 700)]
            require(len(failures) == 1 and failures[0]['actor']['rawcode'] == 1700885091, 'native251 invariant')
            failure = failures[0]
            counter = failure['clock']['counter']
            tasks = [x for x in a if x['event'] == 'target-task' and x['scene'] == scene and (x['tick'] == scene * 1000 + 100)]
            require(len(tasks) == scene + 1 and all((x['range'] == 1108606976 for x in tasks)), 'native251 invariant')
            require(tasks[0]['unit']['identity'] == failure['actor']['identity'], 'native251 invariant')
            require(any((x['event'] == 'blocked' and x['scene'] == scene and (x['partial'] == 0) and (x['clock']['counter'] == counter) for x in a)), 'native251 invariant')
            releases = [x for x in a if x['event'] == 'reservation-release-enter' and x['scene'] == scene and (x['clock']['counter'] == counter)]
            require(len(releases) == scene + 1 and all((x['marker']['refs'] == 1 for x in releases)), 'native251 invariant')
            stops = [x for x in a if x['event'] == 'marker' and x['scene'] == scene and (x['clock']['counter'] == counter) and ('label=issued id=851972' in x['value'])]
            require(len(stops) == scene, 'native251 invariant')
            if stops:
                require('actor=2 target=0' in stops[0]['value'], 'native251 invariant')
        scenes = []
        for scene in range(3):
            before_retry = scene * 1000 + 700
            failures = [x for x in out if x['event'] == 'cant-path' and x['scene'] == scene and (x['tick'] < before_retry)]
            scenes.append(dict(scene=scene, terminal_failures=failures, blocked=[x for x in out if x['event'] == 'blocked' and x['scene'] == scene], public=[x for x in ms if f' scene={scene} ' in x and any((k in x for k in ['label=initial-return', 'label=retry-return', 'label=peer-return', 'label=before-retry', 'label=abilities']))]))
        summaries.append(scenes)
    require(public[0] == public[1] == public[2], 'public repeat/control mismatch')
    require(native[0] == native[1], 'scoped native mismatch')
    require(summaries[0] == summaries[1], 'native251 invariant')
    require(any(('scene=2 label=retry-return accepted=1' in x for x in public[0])), 'fresh candidate eligibility control failed')
    require(len(public[0]) == 478 and len(native[0]) == 727, 'complete projection counts')
    hooks = [x for x in rows(R / 'observe1.jsonl') if x['event'] == 'marker']
    typed = []
    for x in hooks:
        q = dict(re.findall('(\\w+)=([^ ]+)', x['value']))
        if q['label'] not in ['issued', 'spell']:
            continue
        typed.append(dict(scene=x['scene'], tick=x['tick'], kind=0 if q['label'] == 'issued' else 1, code=int(q['id'] if q['label'] == 'issued' else q['event']), actor=int(q['actor']), target=int(q.get('target', 0)), ability=int(q.get('ability', 0)), clock_word=x['clock']['primary'][0]))
    result = dict(schema=1, public=public[0], hook_prefix=[x['value'] for x in hooks[:3]], scoped_native=native[0], typed_events=typed, scenes=summaries[0], probe_jass=(R / 'itemdrop_probe.j').read_text(), map_metadata=json.loads((R / 'RS-TARGET251-couple-cantpath-v1.json').read_text()), profile={'aliases': ['Aco2', 'Aco3'], 'authored_range_word': 1108606976, 'stock_area': 900, 'Reht': 1, 'owner': 0, 'world_bounds': [0, 0, 2048, 2048], 'pathing_dimensions': [64, 64], 'wall_fine_x': [20, 23], 'wall_fine_y': [0, 63], 'wall_mask': 206, 'positions': {'Archer': [512, 512, 0], 'Hippo': [1024, 512, 180], 'freshArcher': [1100, 512, 180]}}, static_abi=json.loads((R / 'abi-static.json').read_text()), limitations=['Hook tick is last public marker; clock/counter are authoritative.', 'No complete numeric movement parity claim;727 scoped rows select documented fields without pointer normalization.', 'Archer fails first in paired scene; peer cancellation is not a second independent physical failure.', 'Fresh candidate eligibility is independently checked by scene2.', 'Native275FINISH276ENDCAST; ability integers are nativeFourCC and require typed normalization.'])
    raw,typed_polls=project_polls(R/"observe1.jsonl")
    require((raw,typed_polls)==project_polls(R/"observe2.jsonl"),"all160 raw poll fields/order repeat")
    result.update(raw_polls=raw,typed_polls=typed_polls,poll_engine_indices={str(k):v for k,v in POLL_INDEX.items()},poll_roles=["originalArcher userdata1","Hippo userdata2","freshArcher userdata3"])
    result["limitations"].append("160 raw callback rows retain all captured fields; only mover pointers map to actor roles. Typed engine oracle is phase0 ten fields/phase2 six fields, excludes phase1 and unobserved distance. Counter and other raw fields remain in raw_polls.")
    check_semantics(result)
    return result

def check_semantics(d):
 a=d['scoped_native']
 for scene in [0,1]:
  failure=[x for x in a if x['event']=='cant-path' and x['scene']==scene and x['tick']<scene*1000+700]
  require(len(failure)==1,'one original failure');failure=failure[0];counter=failure['clock']['counter']
  require(failure['actor']['rawcode']==0x65617263,'Archer owns failure')
  tasks=[x for x in a if x['event']=='target-task' and x['scene']==scene and x['tick']==scene*1000+100]
  require(len(tasks)==scene+1 and all(x['range']==0x42140000 for x in tasks),'range37 constructors')
  require(tasks[0]['actor']['identity']==failure['actor']['identity'],'failure owner identity')
  releases=[x for x in a if x['event']=='reservation-release-enter' and x['scene']==scene and x['clock']['counter']==counter]
  require(len(releases)==scene+1 and all(x['marker']['refs']==1 for x in releases),'terminal reservation inverse')
  require(all(a.index(failure)<a.index(x) for x in releases),'release after failure')
  retry=next(x for x in a if x['event']=='marker' and x['scene']==scene and 'label=retry-return accepted=1' in x['value'])
  require(all(a.index(x)<a.index(retry) for x in releases),'release before fresh retry')
  stops=[x for x in a if x['event']=='marker' and x['scene']==scene and x['clock']['counter']==counter and 'label=issued id=851972' in x['value']]
  require(len(stops)==scene,'peer stop count')
  if stops:require('actor=2 target=0' in stops[0]['value'] and max(a.index(x) for x in releases)<a.index(stops[0])<a.index(retry),'peer Stop after releases before retry')
  before=next(x for x in a if x['event']=='marker' and x['scene']==scene and 'label=before-retry ' in x['value'])
  require(not any(x['event']=='marker' and x['scene']==scene and 'label=spell ' in x['value'] for x in a[a.index(tasks[0]):a.index(before)]),'no spell execution on failed approach')
 require(any('scene=2 label=retry-return accepted=1' in x for x in d['public']),'independent eligibility control')

def header_text(d):
 def strings(name,items):return 'static char const *const '+name+'[] = {\n'+''.join('    '+json.dumps(s)+',\n' for s in items)+'};\n'
 source=d['probe_jass'].replace('call Preload(','call CoupleCantPathObserve(')
 result='/* Retail251: literal probe with observation adapter only. */\n#ifndef RETAIL_COUPLE_INSTANT251_CANTPATH_H\n#define RETAIL_COUPLE_INSTANT251_CANTPATH_H\nstatic char const couple_native_probe251[] =\n'+''.join('    '+json.dumps(s)+'\n' for s in source.splitlines(keepends=True))+';\n'
 result+=strings('couple_native_prefix251',d['hook_prefix'])+strings('couple_native_public251',d['public'])
 result+='/* kind0 issued order, kind1 spell. Native275FINISH276ENDCAST. */\ntypedef struct { unsigned scene,tick,kind,code,actor,target; uint32_t ability,clock_word; } coupleNativeEvent251_t;\nstatic coupleNativeEvent251_t const couple_native_events251[] = {\n'
 for x in d['typed_events']:result+='{%d,%d,%d,%d,%d,%d,0x%08xu,0x%08xu},\n'%(x['scene'],x['tick'],x['kind'],x['code'],x['actor'],x['target'],x['ability'],x['clock_word'])
 return result+'};\n'+poll_header(d)+'#endif\n'
def compare(a,b):require(a==b,'whole ordered projection')
def verify_header(text,d):require(text==header_text(d),'semantic generated header')
def mutation_checks(d):
 checks=[]
 for key in ['public','scoped_native','typed_events','raw_polls','typed_polls']:
  for kind in ['missing','extra','reordered','field']:
   x=copy.deepcopy(d);a=x[key]
   if kind=='missing':a.pop()
   elif kind=='extra':a.append(copy.deepcopy(a[-1]))
   elif kind=='reordered':a[0],a[1]=a[1],a[0]
   elif key=='public':a[0]+=' MUTATION'
   elif key=='typed_polls':a[0]['words'][0]^=1
   else:a[0]['tick']+=1
   try:compare(x,d)
   except ValueError:checks.append(key+':'+kind)
   else:raise ValueError('accepted projection mutation')
 for kind in ['range','failure-owner','release-refs','stop-actor']:
  x=copy.deepcopy(d);a=x['scoped_native']
  if kind=='range':next(z for z in a if z['event']=='target-task')['range']+=1
  elif kind=='failure-owner':next(z for z in a if z['event']=='cant-path')['actor']['identity'][0]+=1
  elif kind=='release-refs':next(z for z in a if z['event']=='reservation-release-enter')['marker']['refs']=2
  else:
   z=next(z for z in a if z['event']=='marker' and z['scene']==1 and 'label=issued id=851972' in z['value']);z['value']=z['value'].replace('actor=2','actor=1')
  try:check_semantics(x)
  except ValueError:checks.append('semantic:'+kind)
  else:raise ValueError('accepted semantic mutation')
 text=header_text(d);one=json.dumps(d['public'][0])+',';two=json.dumps(d['public'][1])+','
 variants=[text.replace(one,'',1),text.replace(one,one+one,1),text.replace(one,'SWAP',1).replace(two,one,1).replace('SWAP',two,1),text.replace('0x%08xu'%d['typed_events'][0]['clock_word'],'0x00000000u',1)]
 for i,t in enumerate(variants):
  try:verify_header(t,d)
  except ValueError:checks.append('header:'+str(i))
  else:raise ValueError('accepted header mutation')
 lines=text.splitlines(keepends=True);start=next(i for i,line in enumerate(lines) if 'couple_native_polls251[]' in line)+1
 for mode in ['missing','extra','reordered','field']:
  a=lines[:]
  if mode=='missing':a.pop(start)
  elif mode=='extra':a.insert(start,a[start])
  elif mode=='reordered':a[start],a[start+1]=a[start+1],a[start]
  else:a[start]=a[start].replace('0x','0y',1)
  try:verify_header(''.join(a),d)
  except ValueError:checks.append('poll-header:'+mode)
  else:raise ValueError('accepted poll header mutation')
 return checks

def verify(capture_dir,fixture=FIXTURE,header=None):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text());require(sha(fixture)==p['projection_sha256'],'fixture pin')
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(capture_dir)/n)==h,'capture pin '+n)
 d=project(capture_dir);compare(d,json.loads(fixture.read_text()));h=header or fixture.with_suffix('.h');require(sha(h)==p['header_sha256'],'header pin');verify_header(h.read_text(),d);require(len(mutation_checks(d))==32,'negative controls');return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--header',type=pathlib.Path);p.add_argument('--check',action='store_true');a=p.parse_args();d=verify(a.capture_dir,a.fixture,a.header);print('PASS251:478public/727scoped/%d typed events;32negative controls'%len(d['typed_events']))
