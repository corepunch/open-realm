#!/usr/bin/env python3
"""Verify complete archived selected and independent public Move lifetimes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

# Current owner time comes from the next pre-owner mover clock. The final
# time is inferred from six native positive 5ms additions, whose progression
# is checked against every observed interval. No final post-commit is claimed.
def add(a,b):
 ea=(a>>23)&255;eb=(b>>23)&255;e=min(ea,eb)
 m=((a&0x7fffff)|0x800000)<<(ea-e);m+=((b&0x7fffff)|0x800000)<<(eb-e)
 shift=max(0,m.bit_length()-24);m>>=shift
 return ((e+shift)<<23)|(m&0x7fffff)
def advance(a):
 for _ in range(6):a=add(a,0x3ba3d70a)
 return a

def extract(records):
 groups={};visits=[];current={}
 for x in records:
  if x.get('event')=='tick':
   key=tuple(x['identity']);visit=dict(before=x,events=[],ordinal=len(groups.get(key,[])))
   groups.setdefault(key,[]).append(visit);visits.append(visit);current[x['group']]=visit
  elif x.get('group') in current:current[x['group']]['events'].append(x)
 first=[v[0]['before'] for v in groups.values()]
 identities=sorted({tuple(m['identity']) for t in first for m in t['members']});ids={k:i for i,k in enumerate(identities)}
 movers={m['mover']:ids[tuple(m['identity'])] for t in first for m in t['members']}
 for group in groups.values():
  valid=[v for v in group if any('mover' in m for m in v['before']['members'])]
  for i,v in enumerate(valid):
   v['clock']=next(m['time'] for m in valid[i+1]['before']['members'] if 'time' in m) if i+1<len(valid) else advance(valid[i-1]['clock'])
  assert all(advance(a['clock'])==b['clock'] for a,b in zip(valid,valid[1:]))
 rows=[];owners=[];first=[]
 for v in visits:
  if 'clock' not in v:continue
  t=v['before'];m=list(t['members']);j=0
  while j<len(m):
   if 'mover' not in m[j]:m[j]=m[-1];m.pop()
   else:j+=1
  members=[[ids[tuple(x['identity'])],*x['offset'],*x['dest'],x['speed'],x['heading'],int(x['flags'],16),*x['pos'],*x['vel'],x['max'],x['facing'],x['radius'],x['range'],x['time'],x['epoch']] for x in m]
  commits=[[movers[x['mover']],int(x['gflags'],16),int(x['mflags'],16),x['req'],x['heading'],x['cap'],x['speed'],*x['pos'],*x['vel'],x['facing'],x['time'],x['epoch']] for x in v['events'] if x['event']=='commit']
  route=next(x for x in v['events'] if x['event']=='route');r=[x for x in v['events'] if x['event']=='regroup']
  rows.append(dict(clock=v['clock'],group=[int(t['flags'],16),t['age'],t['completion'],t['cooldown'],*t['point'],t['heading']],members=members,commits=commits,route=[route['result'],route['ready'],route['final'],route['path']['accCount'],route['path']['accIndex']],regroup=[1,r[0]['result'],r[0]['out1'],r[0]['out2'],r[0]['completion']] if r else [0]*5))
  owners.append(members[0][0]);first.append(v['ordinal']==0)
 goal=next(x['requestPoint'] for x in records if x.get('event')=='cohort')
 admission=rows[0]['members'][0][16]
 finish=next(x['value'] for x in records if x.get('event')=='marker' and 'label=complete' in x['value'])
 points=[finish.split('u%d='%i)[1].split()[0].rsplit(',',1)[0] for i in range(6)]
 return dict(rows=rows,owners=owners,first=first,goal=goal,admission=admission,finish=points,groups=len(groups))

def carray(a):return '{'+','.join('0x%08xu'%x for x in a)+'}'

def render(scenes):
 h=['/* Original FORM-05.1 selected repeats and independent singleton control. */']
 for i,s in enumerate(scenes):
  h.append('static typeof(formation169_passage[0]) const formation170_scene%d[]={'%i)
  for x in s['rows']:h.append(' {'+','.join([hex(x['clock'])+'u',str(len(x['members'])),str(len(x['commits'])),carray(x['group']),'{'+','.join(carray(m) for m in x['members'])+'}','{'+','.join(carray(m) for m in x['commits'])+'}',carray(x['route']),carray(x['regroup'])])+'},')
  h.append('};')
  h.append('static uint8_t const formation170_first%d[]='%i+'{'+','.join(str(int(x)) for x in s['first'])+'};')
  h.append('static char const *const formation170_finish%d[]='%i+'{'+','.join(json.dumps(x) for x in s['finish'])+'};')
 h.append('static struct {typeof(formation169_passage[0]) const *rows;uint8_t const *first;char const *const *finish;uint32_t count,admission,goal[2],groups;} const formation170_scenes[]={')
 for i,s in enumerate(scenes):h.append(' {formation170_scene%d,formation170_first%d,formation170_finish%d,%du,0x%08xu,%s,%du},'%(i,i,i,len(s['rows']),s['admission'],carray(s['goal']),s['groups']))
 h.append('};')
 return "\n".join(h)+"\n"

def verify(frozen, archive, header):
 root=Path(__file__).resolve().parents[3]
 path=Path(__file__).with_name('verify_formation169_passage.py')
 spec=importlib.util.spec_from_file_location('formation169',path)
 prior=importlib.util.module_from_spec(spec);spec.loader.exec_module(prior)
 # Reuse the strict provenance, raw-count, completion, control and original
 # layout validation for all ten delivered archives, including these four.
 base=json.loads((root/frozen['provenance']).read_text())
 prior.verify(base,archive,root/'games/warcraft-3/game/tests/fixtures/retail_formation_passage_169.h')
 def scene(name):
  if name not in base['captures']:raise ValueError('source not provenance-verified: '+name)
  return extract([json.loads(line) for line in (archive/name).read_text().splitlines()])
 scenes=[scene(name) for name in frozen['sources']]
 if scenes[2]!=scene(frozen['independent_repeat']):raise ValueError('independent retail repeat differs')
 generated=render(scenes)
 if header.read_text()!=generated or hashlib.sha256(generated.encode()).hexdigest()!=frozen['header_sha256']:
  raise ValueError('complete native-word engine fixture differs')
 counts=[]
 for s in scenes:
  frames=[]
  for i,row in enumerate(s['rows']):
   if not i or row['clock']!=s['rows'][i-1]['clock']:frames.append(i)
  start=frames[60]
  counts.append(dict(owner_visits=len(s['rows']),member_commits=sum(len(r['commits']) for r in s['rows']),
   groups=s['groups'],saved_suffix_visits=len(s['rows'])-start,
   saved_suffix_commits=sum(len(r['commits']) for r in s['rows'][start:])))
 if counts!=frozen['scenes']:raise ValueError('incomplete scene or saved suffix')
 return dict(status='live-exact-selection-and-independent-move',passed=True,scenes=len(scenes),
  owner_visits=sum(x['owner_visits'] for x in counts),member_commits=sum(x['member_commits'] for x in counts),
  physical_groups=sum(x['groups'] for x in counts),saved_suffix_visits=sum(x['saved_suffix_visits'] for x in counts),
  saved_suffix_commits=sum(x['saved_suffix_commits'] for x in counts),independent_repeats=2)

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for key in ('expected','archive','header','output'):ap.add_argument('--'+key,type=Path,required=True)
 args=ap.parse_args()
 try:result=verify(json.loads(args.expected.read_text()),args.archive,args.header)
 except (ValueError,KeyError,IndexError,OSError,AssertionError,StopIteration) as error:
  result=dict(status='failed',passed=False,error=str(error))
 args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
 return 0 if result['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
