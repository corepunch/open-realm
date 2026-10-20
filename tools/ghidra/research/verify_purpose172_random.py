#!/usr/bin/env python3
"""Verify original CRandData seeding, complete archived draws and engine vectors."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

EXPECTED=Path('tools/ghidra/fixtures/research/NUM-04.6-expected.json')
SHA='16afe54c8ce656b3f445a463535215372d88081057e4363d64710786d437a25c'
ORACLE=Path('tools/ghidra/research/verify_NUM-04.6_streams.py')
HEADER=Path('games/warcraft-3/game/tests/fixtures/retail_purpose_random_172.h')


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


def render(oracle):
    s='/* Original game.dll693710/214140 words; frozen NUM-04.6 oracle. */\n#ifndef RETAIL_PURPOSE_RANDOM_172_H\n#define RETAIL_PURPOSE_RANDOM_172_H\n'
    s+='typedef struct { uint32_t seed; wc3Random_t streams[45]; } retailStreams172_t;\nstatic retailStreams172_t const retail_streams172[]={\n'
    for row in oracle['reseed']:
        s+=' {%du,{%s}},\n'%(row['seed'],','.join('{%du,%du}'%tuple(v) for v in row['streams']))
    s+='};\ntypedef struct { uint32_t seed; wc3Random_t owner,streams[45]; } retailReseed172_t;\nstatic retailReseed172_t const retail_reseed172[]={\n'
    for row in oracle['set_random_seed']:
        s+=' {%du,{%du,%du},{%s}},\n'%(row['seed'],*row['owner_after'],','.join('{%du,%du}'%tuple(v) for v in row['streams']))
    s+='};\ntypedef struct { unsigned index; bool real; uint32_t span,result; wc3Random_t before,after; } retailDraw172_t;\nstatic retailDraw172_t const retail_draws172[]={\n'
    for row in oracle['consumers']:
        if row['fn'] not in ('6f693660','6f6936a0'):continue
        s+=' {%d,%s,%du,%du,{%du,%du},{%du,%du}},\n'%(row['index'],
            'true' if row['fn']=='6f6936a0' else 'false',row.get('span',0),
            row.get('result_word',row.get('result')),*row['before'],*row['after'])
    return s+'};\n#endif\n'


def live_projection(analyzed):
    streams={}
    for t in analyzed['timeline']:
        if t['what']=='draw' and t['cls'].startswith('stream:'):
            streams.setdefault(t['cls'],[]).append({k:t[k] for k in ('caller','before','after','value','visit')})
    return dict(game_seed=analyzed['game_seed'],locked=analyzed['locked'],
        checks={k:v for k,v in analyzed['checks'].items() if 'stream' in k or 'reseed' in k},
        stream_draws=dict(sorted(streams.items())),
        reseeds=[t for t in analyzed['timeline'] if t['what'] in ('streams-reseed','SetRandomSeed')])


def check_live(actual,expected):
    assert actual==expected,'purpose identity, draw words/order or reseed state differ'


def verify(oracle,expected,archive):
    assert expected['layout']['count']==45
    assert expected['layout']['stream_offset']=='4 + 8*index'
    for key in expected['oracle']:assert oracle[key]==expected['oracle'][key],key
    assert HEADER.read_text()==render(expected['oracle'])
    startup=module('startup172',Path('tools/ghidra/research/verify_startup171_seed.py'))
    manifest=json.loads(Path('tools/ghidra/fixtures/retail-startup-seed171-1.27.json').read_text())
    provenance=json.loads(startup.PROVENANCE.read_text())
    checked=startup.verify(manifest,archive,provenance)
    analyzer=module('purpose172_analyzer',startup.ANALYZER)
    analyzer.WORDS=json.loads(Path('tools/ghidra/fixtures/retail-pathfinding-random-1.27.json').read_text())['table_words']
    for key,frozen in expected['live'].items():
        path=archive/Path(key).parent/'captures'/Path(key).name/'capture.jsonl'
        check_live(live_projection(analyzer.analyze(path)),frozen)
    for key,frozen in expected['other_generators'].items():
        rows=[json.loads(x) for x in (archive/'NUM-04.5/captures'/key/'capture.jsonl').read_text().splitlines()]
        end=next(x for x in rows if x['event']=='trace-end')
        other={k:v for k,v in sorted(end['agg'].items()) if not k.startswith(('owner@','stream'))}
        assert other==frozen,key
    return dict(passed=True,status='original-exact-purpose-random',binary_sha256=expected['binary_sha256'],
        original_calls=oracle['original_calls'],tls_lookups=oracle['tls_lookups'],streams=45,
        reseed_seeds=len(oracle['reseed']),public_reseeds=len(oracle['set_random_seed']),
        consumer_calls=len(oracle['consumers']),captures=checked['captures'],stream_draws=checked['stream_draws'],
        owner_draws=checked['owner_draws'],observer_free_comparisons=checked['observer_free_comparisons'],
        stand_ins=oracle['stand_ins'],incomplete_captures=checked['incomplete_captures'])


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True)
    ap.add_argument('--archive',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    raw=EXPECTED.read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
    report=args.output.with_suffix('.original.json');fixture=args.output.with_suffix('.words.json')
    subprocess.run([sys.executable,str(ORACLE),'--binary',str(args.binary),'--report',str(report),'--fixture',str(fixture)],check=True)
    result=verify(json.loads(fixture.read_text()),json.loads(raw),args.archive)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
