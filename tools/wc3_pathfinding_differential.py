#!/usr/bin/env python3
"""Exact public-game differential reports; currently the four-lifetime baseline.

No address-based alignment, float epsilon, resampling or dropped events. This
bounded contract does not certify routes, RNG, all movement or presentation.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_fine_result_trace import verify,same_cell_motion

SCENARIO='same-cell-four-lifetimes-114'
TEST='wc3_movement.public_same_cell_routes_retain_single_points_and_saved_motion'
FIXTURES=ROOT/'tools/ghidra/fixtures'
SPEC=FIXTURES/'retail-differential-inputs-115.json'

def digest(raw):return hashlib.sha256(raw).hexdigest()
def load(path):return json.loads(Path(path).read_text())
def write(path,value):Path(path).write_text(json.dumps(value,indent=2)+'\n')
def read_rows(raw):return [json.loads(line)for line in raw.splitlines()if line.strip()]
def inputs():return load(SPEC)
def exact(a,b):return json.dumps(a,sort_keys=True,separators=(',',':'))==json.dumps(b,sort_keys=True,separators=(',',':'))

def report(events,provenance):
    result={'version':1,'scenario':SCENARIO,'inputs':inputs(),'events':events,'provenance':provenance,'presentation':{}}
    validate(result)
    return result

def retail_report(path):
    path=Path(path);packed=path.read_bytes();raw=gzip.decompress(packed)if path.suffix=='.gz'else packed
    fixture=load(FIXTURES/'retail-cached-fine-route-1.27-live.json')
    cap=next((c for c in fixture['captures']if c['sha256']==digest(raw)and c['bytes']==len(raw)),None)
    if cap is None:raise ValueError('retail capture is not an accepted complete baseline')
    rows=read_rows(raw);verify(rows,fixture,cap)
    same_cell_motion(rows,load(FIXTURES/'retail-same-cell-motion-1.27.json'))
    events=[];life=-1;owners={};profiles=inputs()['journal_profiles'];seen=set()
    for r in rows:
        if r['event']=='marker':
            match=re.search(r'case=(\d+)',r['value'])
            if match:life=int(match[1])
        if 0<=life<4 and r['event']=='motion-decision':
            if any(r[k]!=profiles[life][k]for k in ('turn','window')):raise ValueError('retail turn/window input differs')
            seen.add(('turn',life))
        if 0<=life<4 and r['event']=='mover-radius-state':
            if r['radius']!=profiles[life]['radius']:raise ValueError('retail collision input differs')
            seen.add(('radius',life))
        if r['event']!='velocity-commit' or not 0<=life<4:continue
        # Bind identities by the producer lifetime, never by sorting addresses.
        owner=owners.setdefault(life,r['mover'])
        if owner!=r['mover']:raise ValueError('retail lifetime has multiple physical movers')
        if r['clock'][:2]!=r['after'][:2]:raise ValueError('retail callback clock differs from committed clock')
        events.append(dict(event='velocity-commit',sequence=len(events),actor=[life,0],
            clock=r['clock'],position=r['after'][2:4],velocity=r['after'][4:6],heading=r['after'][7]))
    if len(seen)!=8:raise ValueError('retail input profile observations missing')
    return report(events,dict(adapter='retail-read-only-frida',raw_sha256=digest(raw),
        capture=cap['metadata'],input_spec_sha256=digest(SPEC.read_bytes())))

def engine_report(path,provenance=None):
    raw=Path(path).read_bytes();rows=read_rows(raw);spec=inputs()
    begin={'event':'begin','scenario':SCENARIO,**spec['journal_begin']}
    if not rows or not exact(rows[0],begin):raise ValueError('engine journal inputs differ or beginning missing')
    if not exact(rows[-1],{'event':'end','count':28}):raise ValueError('engine journal incomplete')
    profiles=[r for r in rows[1:-1]if r.get('event')=='profile']
    if not exact(profiles,spec['journal_profiles']):raise ValueError('engine effective movement inputs differ')
    if len(rows)!=34 or [i for i,r in enumerate(rows)if r.get('event')=='profile']!=[1,9,17,25]:
        raise ValueError('engine input profile publication order differs')
    events=[r for r in rows[1:-1]if r.get('event')!='profile']
    return report(events,dict(adapter='open-realm-public-game',raw_sha256=digest(raw),
        input_spec_sha256=digest(SPEC.read_bytes()),**(provenance or {})))

def validate(r):
    if type(r)is not dict or set(r)!={'version','scenario','inputs','events','provenance','presentation'}:
        raise ValueError('report envelope fields differ')
    if type(r['version'])is not int or r['version']!=1 or r['scenario']!=SCENARIO:raise ValueError('unknown report contract')
    if type(r['inputs'])is not dict or type(r['events'])is not list:raise ValueError('input/event types differ')
    if type(r['provenance'])is not dict:raise ValueError('provenance must be an object')
    # Only these transport/render diagnostics are exempt from simulation equality.
    if type(r['presentation'])is not dict or set(r['presentation'])-{'wall_ms','swap_intervals_ms','image_sha256'}:
        raise ValueError('unknown presentation field; simulation fields cannot be exempted')
    if len(r['events'])!=28:raise ValueError('expected all 28 ordered commits')
    for i,e in enumerate(r['events']):
        if type(e)is not dict or set(e)!={'event','sequence','actor','clock','position','velocity','heading'}:
            raise ValueError(f'/events/{i}: event fields differ')
        if e['event']!='velocity-commit' or type(e['sequence'])is not int or e['sequence']!=i:
            raise ValueError(f'/events/{i}/sequence: ordered event sequence differs')
        if type(e['actor'])is not list or len(e['actor'])!=2 or any(type(w)is not int for w in e['actor']):
            raise ValueError(f'/events/{i}/actor: identity must be two integers')
        if e['actor']!=[i//7,0]:raise ValueError(f'/events/{i}/actor: producer lifetime order differs')
        for key,n in [('clock',3),('position',2),('velocity',2),('heading',1)]:
            words=[e[key]]if n==1 else e[key]
            if type(words)is not list or len(words)!=n or any(type(w)is not int or not 0<=w<=0xffffffff for w in words):
                raise ValueError(f'/events/{i}/{key}: expected uint32 words')

def compare(expected,actual):
    differences=[]
    for side,r in [('expected',expected),('actual',actual)]:
        try:validate(r)
        except ValueError as e:differences.append(dict(path='/',side=side,error=str(e)))
    if differences:
        return dict(contract=1,passed=False,difference_count=len(differences),differences=differences,
            ignored=['provenance','presentation'],simulation_tolerance='exact uint32 and ordered structure')
    def visit(a,b,path):
        if type(a)is not type(b):differences.append(dict(path=path,expected=a,actual=b,error='type differs'));return
        if type(a)is dict:
            for k in sorted(set(a)|set(b)):
                p=path+'/'+k.replace('~','~0').replace('/','~1')
                if k not in a or k not in b:differences.append(dict(path=p,error='extra field'if k not in a else 'missing field'))
                else:visit(a[k],b[k],p)
        elif type(a)is list:
            if len(a)!=len(b):differences.append(dict(path=path,error='length differs',expected=len(a),actual=len(b)))
            for i,(x,y)in enumerate(zip(a,b)):visit(x,y,path+'/'+str(i))
        elif a!=b:
            d=dict(path=path,expected=a,actual=b,error='value differs')
            if type(a)is int and type(b)is int:d.update(expected_hex=f'0x{a:08x}',actual_hex=f'0x{b:08x}')
            if path.startswith('/events/'):
                i=int(path.split('/')[2]);d['event_context']={k:expected['events'][i].get(k)for k in ('sequence','actor','event')}
            differences.append(d)
    for k in ('version','scenario','inputs','events'):visit(expected.get(k),actual.get(k),'/'+k)
    return dict(contract=1,passed=not differences,difference_count=len(differences),differences=differences[:32],
        ignored=['provenance','presentation'],simulation_tolerance='exact uint32 and ordered structure')

def run(binary,data,output,tft=False):
    binary=Path(binary).resolve();data=Path(data).resolve();output=Path(output).resolve()
    output.mkdir(parents=True,exist_ok=False) # No stale reports or concurrent reuse.
    cmd=[str(binary),'-data',str(data)]
    if tft:cmd+=['-tft']
    cmd+=['+dedicated','1','+test',TEST]
    journal=output/'engine.jsonl'
    result=subprocess.run(cmd,cwd=ROOT,env=dict(os.environ,WC3_PATHFINDING_TRACE=str(journal)),
        capture_output=True,text=True,timeout=120)
    log=result.stdout+result.stderr;(output/'engine.log').write_text(log)
    # The engine's test command can exit zero with failed assertions.
    totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log)
    if result.returncode or len(totals)!=1 or any(int(a)!=int(b)or not int(a)or int(n)!=1 for a,b,n in totals):
        raise ValueError('engine regression did not pass; inspect engine.log')
    provenance={'binary_sha256':digest(binary.read_bytes()),'tft':tft,'command':cmd,
        'game_source_sha256':digest((ROOT/'games/warcraft-3/game/skills/s_move.c').read_bytes()),
        'test_source_sha256':digest((ROOT/'games/warcraft-3/game/tests/t_movement.c').read_bytes())}
    provenance['test_assets']={str(p.relative_to(data)):digest(p.read_bytes())for p in sorted(data.rglob('*.mpq'))}
    library=binary.parent.parent/'lib/libgame-wc3-test.so'
    if library.exists():provenance['game_library_sha256']=digest(library.read_bytes())
    actual=engine_report(journal,provenance);write(output/'engine-report.json',actual)
    results=[]
    for tag in ('b','c'):
        expected=retail_report(FIXTURES/f'retail-cached-fine-route-1.27-live-{tag}.jsonl.gz')
        write(output/f'retail-{tag}-report.json',expected);results.append(compare(expected,actual))
    summary={'passed':all(r['passed']for r in results),'captures':results}
    write(output/'comparison.json',summary)
    return summary

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='mode',required=True)
    for name in ('retail','engine'):
        q=sub.add_parser(name);q.add_argument('input',type=Path);q.add_argument('output',type=Path)
    q=sub.add_parser('compare');q.add_argument('expected',type=Path);q.add_argument('actual',type=Path);q.add_argument('output',type=Path)
    q=sub.add_parser('run');q.add_argument('--binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');q.add_argument('--data',type=Path,default=ROOT/'build/tests');q.add_argument('--output',type=Path,required=True);q.add_argument('--tft',action='store_true')
    a=p.parse_args()
    try:
        if a.mode=='run':r=run(a.binary,a.data,a.output,a.tft)
        elif a.mode=='compare':r=compare(load(a.expected),load(a.actual));write(a.output,r)
        else:r=(retail_report if a.mode=='retail'else engine_report)(a.input);write(a.output,r)
    except (ValueError,OSError,subprocess.SubprocessError)as e:
        print(str(e),file=sys.stderr);return 1
    if 'passed'in r:
        print(json.dumps({'passed':r['passed'],'report':str(a.output)}));return 0 if r['passed']else 1
    return 0

if __name__=='__main__':raise SystemExit(main())
