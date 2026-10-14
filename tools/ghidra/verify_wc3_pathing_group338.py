#!/usr/bin/env python3
"""Verify neutral-owner levels and artillery acquisition against retail calls.

Includes Town AI stock-candidate words and original scalar boundary blocks.
This verifier does not claim complete GROUP-03.2 closure.
"""
import argparse, copy, gzip, hashlib, json, re
from collections import Counter
from pathlib import Path
from retail_capture_source import matches_capture_source
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-group338-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group338-inputs-1.27.json.gz'
TOWN_BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group338-town-inputs-1.27.json.gz'
STATES_BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group338-states-inputs-1.27.json.gz'
TESTS=['wc3_order_lifecycle.*','wc3_attack_orders.*','wc3_combat.*',
       'wc3_spell.approach185*','wc3_spell.entangling_roots*']

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def public(text):return re.findall(r'call Preload\( "(G338 [^"\r\n]*)" \)',text)

def projection(rows):
    result=[];identities={};scene=None
    for row in rows:
        if 'seq' not in row or row['event']=='module':continue
        if row['scene']!=scene:scene=row['scene'];identities={}
        def identity(value):
            if value not in identities:identities[value]=len(identities)
            return identities[value]
        row=copy.deepcopy(row);row.pop('seq');row.pop('ability',None);row.pop('receiver',None)
        # The probe destroys every actor at scene end. Allocator pointer reuse
        # is not cross-scene identity; retain equality relationships per scene.
        for key in ('actor','candidate'):
            unit=row.get(key)
            if isinstance(unit,int):row[key]=identity(unit)
            elif isinstance(unit,dict):
                unit['identity']=identity(unit['identity'])
                unit['move_present']=unit.pop('move')!='0x0'
                if unit['attack']:unit['attack'].pop('pointer')
        if row['event']=='artillery':
            row['descriptor']={str(i*4):row['descriptor'][i] for i in (8,10,12,14)}
        result.append(row)
    return result

def runtime(cases,spec):
    if len(cases)!=3 or [c['rows'][0]['mode'] for c in cases]!=['observe','observe','control']:
        raise ValueError('missing repeat/control')
    for case in cases:
        rows=case['rows'];meta=rows[0];markers=public(case['preload'])
        if (meta.get('sha256')!=SHA or meta.get('task')!='GROUP-03.2' or not meta.get('owned') or
            meta.get('env') not in ('B','C') or meta['source_sha256']!=spec['capture_sources'] or
            any(r.get('type')=='error' or r.get('event') in ('compile-error','trace-failed') for r in rows)):
            raise ValueError('capture provenance/failure')
        if len(markers)!=338 or markers!=spec['public'] or rows[-1]!={
            'event':'preload-file','sha256':hashlib.sha256(case['preload'].encode()).hexdigest(),
            'markers':338,'complete':True}:raise ValueError('public stream incomplete or changed')
        if meta['mode']=='control':
            if any('seq' in r or r['event']=='trace-end' for r in rows):raise ValueError('instrumented control')
            continue
        ends=[r for r in rows if r['event']=='trace-end']
        if (len(ends)!=1 or not ends[0].get('readOnly') or not ends[0].get('installed') or ends[0]['depth'] or
            ends[0]['counts']!=dict(Counter(r['event'] for r in rows if 'seq' in r))):
            raise ValueError('unfinished native observer')
        if projection(rows)!=spec['events'] or [r['value'] for r in rows if r['event']=='marker']!=markers:
            raise ValueError('native event stream differs')
        for scene in range(78,84):
            selected=[r for r in rows if r.get('scene')==scene]
            actors=[r['actor']['identity'] for r in selected if r['event']=='notify' and r['actor']['rawcode']==0x686d746d]
            if len(actors)!=1:raise ValueError('missing artillery actor')
            ranks=[r['key'] for r in selected if r['event']=='rank-end' and r['actor']==actors[0]]
            high=[0x0be00000,0x09600000,0x08600000,0,0,0][scene-78]
            if not ranks or ranks[0]!=([0x64000000,high] if high else [0,0]):raise ValueError('collateral rank differs')
            decisions=[r['accepted'] for r in selected if r['event']=='compare-end' and r['actor']==actors[0]]
            if decisions!=[int(scene<80)]:raise ValueError('collateral decision differs')
        for scene,high in ((76,0xabc01000),(77,0xabe05000)):
            selected=[r for r in rows if r.get('scene')==scene]
            rank=next(r for r in selected if r['event']=='rank-end')
            if rank['key']!=[0x64000000,high]:raise ValueError('neutral level decision differs')
    return dict(captures=3,scenes=84,public_markers=338,native_events=3198,artillery_domains=6,neutral_level_domains=2)

def town_runtime(cases,spec):
    from research.group338_town import rows_from_events,header
    if len(cases)!=3 or [c['rows'][0]['mode'] for c in cases]!=['observe','observe','control']:
        raise ValueError('missing Town repeat/control')
    for index,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];markers=public(case['preload'])
        if (meta.get('sha256')!=SHA or meta.get('task')!='GROUP-03.2' or not meta.get('owned') or
            meta.get('env') not in ('B','C') or meta['source_sha256']!=spec['capture_sources'][index] or
            any(r.get('type')=='error' or r.get('event') in ('compile-error','trace-failed') for r in rows)):
            raise ValueError('Town capture provenance/failure')
        if len(markers)!=338 or markers!=spec['public'] or rows[-1]!={
            'event':'preload-file','sha256':hashlib.sha256(case['preload'].encode()).hexdigest(),
            'markers':338,'complete':True}:raise ValueError('Town public stream incomplete or changed')
        if meta['mode']=='control':
            if any('seq' in r or r['event']=='trace-end' for r in rows):raise ValueError('instrumented Town control')
            continue
        ends=[r for r in rows if r['event']=='trace-end']
        if (len(ends)!=1 or not ends[0].get('readOnly') or not ends[0].get('installed') or ends[0]['depth'] or
            ends[0]['counts']!=dict(Counter(r['event'] for r in rows if 'seq' in r))):
            raise ValueError('unfinished Town native observer')
        if projection(rows)!=spec['events'] or [r['value'] for r in rows if r['event']=='marker']!=markers:
            raise ValueError('Town native stream differs')
        if rows_from_events(rows)!=spec['rows']:raise ValueError('Town dependency/priority words differ')
    if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_group338_town.h').read_text()!=header(spec['rows']):
        raise ValueError('Town engine fixture differs')
    return dict(town_native_events=len(spec['events']),town_candidate_cases=len(spec['rows']))

def validate(spec):
    if spec.get('version')!=1 or spec.get('task')!='GROUP-03.2' or spec.get('game_sha256')!=SHA:
        raise ValueError('fixture contract differs')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('source changed '+path)
    if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
    if digest(TOWN_BUNDLE)!=spec['town']['bundle_sha256']:raise ValueError('Town bundle changed')
    if digest(STATES_BUNDLE)!=spec['states']['bundle_sha256']:raise ValueError('control-effect bundle changed')
    return spec

def states_runtime(cases,spec):
    if len(cases)!=3 or [c['rows'][0]['mode'] for c in cases]!=['observe','observe','control']:
        raise ValueError('missing control-effect repeat/control')
    for index,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];markers=public(case['preload'])
        if (meta.get('sha256')!=SHA or meta.get('task')!='GROUP-03.2' or not meta.get('owned') or
            meta.get('env') not in ('B','C') or meta['source_sha256']!=spec['capture_sources'][index] or
            any(r.get('type')=='error' or r.get('event') in ('compile-error','trace-failed') for r in rows)):
            raise ValueError('control-effect provenance/failure')
        if len(markers)!=30 or markers!=spec['public'] or rows[-1]!={
            'event':'preload-file','sha256':hashlib.sha256(case['preload'].encode()).hexdigest(),
            'markers':30,'complete':True}:raise ValueError('control-effect public stream differs')
        if meta['mode']=='control':
            if any('seq' in r or r['event']=='trace-end' for r in rows):raise ValueError('instrumented control-effect control')
            continue
        ends=[r for r in rows if r['event']=='trace-end']
        if (len(ends)!=1 or not ends[0].get('readOnly') or not ends[0].get('installed') or ends[0]['depth'] or
            ends[0]['counts']!=dict(Counter(r['event'] for r in rows if 'seq' in r))):
            raise ValueError('unfinished control-effect observer')
        if projection(rows)!=spec['events'] or [r['value'] for r in rows if r['event']=='marker']!=markers:
            raise ValueError('control-effect native stream differs')
        roots=[r for r in rows if r.get('scene')==2]
        first=next(i for i,r in enumerate(roots) if r['event']=='rank')
        end=next(i for i in range(first+1,len(roots)) if roots[i]['event']=='rank-end')
        observed=roots[first:end+1]
        predicates={(r['address'],r['result'])for r in observed if r['event']=='predicate'}
        if not {('5fb040',0),('48e260',1),('495b90',0)}<=predicates:
            raise ValueError('missing Roots mobility/prevention witnesses')
        if observed[-1]['key']!=[0x62000000,0x7ba01000]:raise ValueError('Roots priority differs')
    return dict(control_effect_native_events=len(spec['events']),control_effect_scenes=7)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    parser.add_argument('--data',type=Path,default=ROOT/'build/tests')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():parser.error('fresh report required')
    spec=validate(json.loads(SPEC.read_text()));cases=json.loads(gzip.decompress(BUNDLE.read_bytes()))
    live=runtime(cases,spec);original_bytes(args.binary,spec['instructions'])
    from research.group338_tables import original
    if original(args.binary)!=spec['ability_tables']:
        raise ValueError('original ability hierarchy/virtual inventory differs')
    live.update(town_runtime(json.loads(gzip.decompress(TOWN_BUNDLE.read_bytes())),spec['town']))
    live.update(states_runtime(json.loads(gzip.decompress(STATES_BUNDLE.read_bytes())),spec['states']))
    from research.group338_thresholds import original as thresholds,header as threshold_header
    if thresholds(args.binary)!=spec['thresholds']:
        raise ValueError('fresh original threshold blocks differ')
    if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_group338_thresholds.h').read_text()!=threshold_header(spec['thresholds']):
        raise ValueError('threshold engine fixture differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(args.test_binary,args.data,args.report)
    finally:runner.TESTS=previous
    result=dict(passed=True,task='GROUP-03.2',**live,instructions=len(spec['instructions']),engine=engine,
        binary_sha256=SHA,fixture_sha256=digest(SPEC),bundle_sha256=digest(BUNDLE),
        test_binary_sha256=digest(args.test_binary),game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
        limits=spec['limits'])
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
