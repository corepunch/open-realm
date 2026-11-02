#!/usr/bin/env python3
"""Verify minimum-range availability guards against complete original captures."""
import argparse,gzip,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_group338 import ROOT,SHA,digest,projection
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-group340-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group340-inputs-1.27.json.gz'
TESTS=['wc3_order_lifecycle.*','wc3_attack_orders.*','wc3_combat.*','wc3_shadowmeld.*',
       'wc3_unit.neutral_creep_natural_sleep_tracks_night_and_wakes_at_dawn',
       'wc3_unit.no_creep_sleep_player_state_blocks_new_natural_sleep',
       'wc3_unit.unit_add_sleep_policy_is_neutral_only_and_wakes_when_disabled','wc3_spell.ensnare*','wc3_save.ensnare*',
       'wc3_spell.creep_regression_web*','wc3_save.creep_web*']

def public(text):return re.findall(r'call Preload\( "(G340 [^"\r\n]*)" \)',text)

def runtime(cases,spec):
    if len(cases)!=3 or [c['rows'][0]['mode']for c in cases]!=['observe','observe','control']:
        raise ValueError('missing repeat/control')
    for index,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];markers=public(case['preload'])
        if (meta.get('sha256')!=SHA or meta.get('task')!='GROUP-03.2' or not meta.get('owned') or
            meta.get('env')not in ('B','C') or meta['source_sha256']!=spec['capture_sources'][index] or
            any(r.get('type')=='error' or r.get('event')in ('compile-error','trace-failed')for r in rows)):
            raise ValueError('capture provenance/failure')
        import hashlib
        if markers!=spec['public'] or len(markers)!=146 or rows[-1]!={
            'event':'preload-file','sha256':hashlib.sha256(case['preload'].encode()).hexdigest(),
            'markers':146,'complete':True}:raise ValueError('public stream differs')
        if meta['mode']=='control':
            if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
            continue
        ends=[r for r in rows if r['event']=='trace-end']
        if (len(ends)!=1 or not ends[0].get('readOnly') or not ends[0].get('installed') or ends[0]['depth'] or
            ends[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r))):
            raise ValueError('unfinished observer')
        if projection(rows)!=spec['events'] or [r['value']for r in rows if r['event']=='marker']!=markers:
            raise ValueError('native stream differs')
        for scene in range(24):
            segment=[r for r in rows if r.get('scene')==scene and r.get('tick')==scene*5+2]
            notifications=[r for r in segment if r['event']=='notify' and r['actor']['owner']==2]
            if len(notifications)!=1:raise ValueError('missing acquiring actor')
            note=notifications[0]
            if note['minimum']!=[0x42f00000,0,0x44160000][scene%3]:
                raise ValueError('minimum range producer differs')
            exempt=[r for r in segment if r['event']=='exempt' and r['actor']==note['actor']['identity']]
            if bool(exempt)!=(scene not in (3,9)):raise ValueError('minimum-range exemption differs')
    return dict(captures=3,scenes=24,public_markers=146,native_events=len(spec['events']))

def validate(spec):
    if spec.get('version')!=1 or spec.get('task')!='GROUP-03.2' or spec.get('game_sha256')!=SHA:
        raise ValueError('fixture contract differs')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('source changed '+path)
    if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
    return spec

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    parser.add_argument('--data',type=Path,default=ROOT/'build/tests')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():parser.error('fresh report required')
    spec=validate(json.loads(SPEC.read_text()))
    live=runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
    original_bytes(args.binary,spec['instructions'])
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(args.test_binary,args.data,args.report)
    finally:runner.TESTS=previous
    result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=engine,
        binary_sha256=SHA,fixture_sha256=digest(SPEC),bundle_sha256=digest(BUNDLE),
        test_binary_sha256=digest(args.test_binary),game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
