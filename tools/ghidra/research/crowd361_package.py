#!/usr/bin/env python3
"""Freeze original repeats, fresh crowd observation and unhooked control."""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
from crowd361_freeze import extract

ROOT=Path(__file__).resolve().parents[3]
def digest(b):return hashlib.sha256(b).hexdigest()
def summary(path):
    raw=path.read_bytes(); rows=[json.loads(x) for x in raw.decode().splitlines()]
    rows=[item for row in rows for item in (row['rows'] if row.get('event')=='batch' else [row])]
    preload=path.with_name(path.stem+'-preload.txt').read_text()
    public=re.findall(r'call Preload\( "(T021 [^"\r\n]*)" \)',preload)
    metadata=next(r for r in rows if r.get('event')=='metadata')
    return dict(sha256=digest(raw),preload=preload,summary=dict(metadata=metadata,public=public,
        failed=any(r.get('event') in ('trace-failed','error') for r in rows),
        complete=any(r.get('event')=='preload-file' and r.get('complete') for r in rows),
        observer=any(r.get('event')=='trace-end' for r in rows),
        trace_end=[r for r in rows if r.get('event')=='trace-end']))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--research',type=Path,required=True)
    args=ap.parse_args(); b=args.research
    source=[b/'captures/observe-c-1.jsonl',b/'captures/observe-c-2.jsonl',b/'payoff361/observe1.jsonl']
    cases=[summary(p) for p in source+[b/'payoff361/control1.jsonl']]
    runs=[extract(p) for p in source]
    reference=cases[0]['summary']['public']
    if any(c['summary']['public']!=reference for c in cases):raise ValueError('public streams differ')
    if any(r['rows']!=runs[0]['rows'] for r in runs):raise ValueError('native owner streams differ')
    original=b/'maps/RS-TARGET-02.1-c.w3m'; fixed=b/'payoff361/Crowd361.w3m'
    bundle=dict(source_runs=runs,captures=cases,public=reference,base_map=base64.b64encode(original.read_bytes()).decode())
    destination=ROOT/'tools/ghidra/fixtures/retail-crowd361-inputs-1.27.json.gz'
    specpath=ROOT/'tools/ghidra/fixtures/retail-crowd361-1.27.json'
    if destination.exists() or specpath.exists():raise ValueError('fresh fixture destinations required')
    destination.write_bytes(gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0))
    prior=json.loads((ROOT/'tools/ghidra/fixtures/retail-target356-1.27.json').read_text())
    instructions=dict(prior['instructions'])
    instructions.update(json.loads((b/'payoff361/ghidra-readback.json').read_text())['instructions'])
    paths=['games/warcraft-3/game/tests/t_target_pursuit_crowd.c',
           'games/warcraft-3/game/tests/fixtures/retail_crowd361.h',
           'tools/frida/research/crowd361_capture.py','tools/frida/research/target021_observer.js',
           'tools/frida/research/target021c_probe.j','tools/frida/research/target021_make_map.py',
           'tools/frida/research/target356_make_map.py','tools/ghidra/research/crowd361_freeze.py',
           'tools/ghidra/research/crowd361_package.py','tools/ghidra/research/Crowd361Evidence.java']
    spec=dict(version=1,task='TARGET-02.1',game_sha256=prior['game_sha256'],engine_tests=['wc3_movement.crowd361*'],
              map_hashes=[digest(original.read_bytes()),digest(fixed.read_bytes())],instructions=instructions,
              pins={p:digest((ROOT/p).read_bytes()) for p in paths},bundle_sha256=digest(destination.read_bytes()),
              projection_sha256=digest(json.dumps(runs[0]['rows'],separators=(',',':')).encode()),
              limits=['Natural36 Smart follower scene only; no unobserved masks or unrelated order families implied.',
                      'Lite observer does not record per-visit retry state; captured22 original words remain literal.',
                      'Existing engine passes baseline; this adds strict parity coverage without an invented engine change.'])
    specpath.write_text(json.dumps(spec,indent=2)+'\n');print(len(instructions),'instruction guards;',len(runs[0]['rows']),'owner rows')
if __name__=='__main__':main()
