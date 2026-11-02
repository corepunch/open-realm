"""Complete bounded native cohort records; literal scalar and identity contract."""
import copy,re
def bounded(raw):
 starts=[i for i,r in enumerate(raw) if r.get('event')=='marker' and r['value'].startswith('T377C tick=40 label=resume-before ')]
 ends=[i for i,r in enumerate(raw) if r.get('event')=='marker' and r['value'].startswith('T377C tick=275 label=complete ')]
 assert len(starts)==len(ends)==1 and starts[0]<ends[0]
 # The host controller's input-completion receipt is transport, not an
 # observer record; it may arrive after the next game tick has advanced.
 return copy.deepcopy([r for r in raw[starts[0]:ends[0]+1] if r['event']!='player-input'])
STATE_EVENTS={'arrival','turn-or-stop','velocity','clear-force','stop','range-publication','force-range'}
UNIT_EVENTS={'internal-dispatch-enter','internal-dispatch-leave','internal-pop-enter','internal-pop-leave','point-task-consume'}
def project(raw):
 rows=bounded(raw);bound={};unit_storage={}
 for r in raw:
  if r.get('event')!='unit-snapshots':continue
  for u in r['units']:
   if not u['mover']:continue
   ident=tuple(u['identity']);storage=u['mover']['mover']
   assert storage!='0x0'
   assert unit_storage.setdefault(ident,storage)==storage
   assert bound.setdefault(storage,ident)==ident
 assert len(bound)==len(unit_storage)==3
 def mover(state):
  if state is not None:
   storage=state['mover'];assert storage in bound
   state['mover']={'unit_identity':list(bound[storage])}
 def unit(u):
  if u['mover']:
   storage=u['mover']['mover'];assert bound[storage]==tuple(u['identity']);mover(u['mover'])
 for r in rows:
  event=r['event']
  if event=='unit-snapshots':
   for u in r['units']:unit(u)
  if event in STATE_EVENTS:
   for side in ['before','after']:
    if side in r:mover(r[side])
  if event in UNIT_EVENTS:unit(r['unit'])
  if event in ['cohort-search-enter','cohort-search-leave']:unit(r['source'])
  if event in ['cohort-candidate-enter','cohort-candidate-leave']:
   for key in ['candidate','source','candidateAfter','sourceAfter']:
    if key in r:unit(r[key])
 return rows

def verify(rows):
 accepted=[r for r in rows if r['event']=='cohort-candidate-leave' and r['accepted']]
 assert len(accepted)==2
 for r in accepted:
  c,s=r['candidate'],r['source']
  assert c['identity']!=s['identity'] and c['previous']==s['previous']==[847,906]
  assert c['category']==s['category']==1 and r['result']==0
  assert r['context'][3:5]==[1111549868,1107924485]
 # Exact candidate-goal output is a separate native read, not inferred from
 # public rounded coordinates or a group-equality assertion.
 fine=[r['point'] for r in rows if r['event']=='candidate-fine-goal']
 assert fine==[[1111549868,1107924485]]*3+[[1099431936,1111752704]]
 snaps=[r for r in rows if r['event']=='unit-snapshots' and r['value'].startswith('T377C tick=138 label=sample ')]
 assert len(snaps)==1
 a,b=snaps[0]['units'][:2]
 assert a['task']==[886,918] and b['task']==[891,934]
 assert a['mover']['group']==b['mover']['group']==[871,942]
 consumed=[r for r in rows if r['event']=='point-task-consume']
 assert [(r['tick'],r['unit']['identity'],r['unit']['task']) for r in consumed]==[
  (60,[799,810],[886,918]),(137,[826,828],[891,934]),(141,[799,810],[885,917]),(143,[826,828],[890,933])]
 created=[r['point'] for r in rows if r['event']=='point-task']
 assert created==[[1141374976,1153695744],[1153492908,1149867525],[1149501440,1153695744],[1153492908,1149867525]]
 assert [r['tick'] for r in accepted]==[60,137]
 return {'accepted_ticks':[60,137],'shared_group':[871,942],'retained_tasks':[[885,917],[890,933]],'cached_exit_words':[[1141374976,1153695744],[1149501440,1153695744]]}
