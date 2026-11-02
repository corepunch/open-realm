#!/usr/bin/env python3
"""Verify original callback interruption streams and actual game/save continuations."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
from interrupt209_fixture import normalize,validate,scene
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-interrupt209-1.27.json'
PORTABLE=ROOT/'tools/ghidra/fixtures/retail-interrupt209-1.27.jsonl.gz'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS=['wc3_interrupt.pending_removal_retains_all_order_shapes_and_saved_head',
       'wc3_interrupt.actual_spell_completion_replaces_owner_and_releases_removed_peers_once']


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def contract(spec):
    expected={'version':1,'task':'ORDER-06.4 / payoff209','binary_sha256':SHA,'rows':2768,
              'mutation_counter':1140,'public_markers':152,'release_callbacks':1,'completed_members':2,
              'destroyed_wrappers':196,'removed_units':4,'owner_intervals':400,'engine_tests':TESTS}
    if any(spec.get(k)!=v for k,v in expected.items()) or len(spec['captures'])!=3:
        raise ValueError('interruption contract differs')
    if digest(PORTABLE)!=spec['portable_sha256']:raise ValueError('portable pin differs')
    raw=gzip.decompress(PORTABLE.read_bytes())
    if hashlib.sha256(raw).hexdigest()!=spec['timeline_sha256']:raise ValueError('raw timeline pin differs')
    rows=[json.loads(l)for l in raw.splitlines()]
    if len(rows)!=2768:raise ValueError('incomplete portable timeline')
    validate(rows)
    return rows


def original(spec,archive):
    expected=contract(spec)
    for name,pin in spec['archive_pins'].items():
        if digest(archive/name)!=pin:raise ValueError('archive changed: '+name)
    for name,pin in spec['sources'].items():
        if digest(ROOT/name)!=pin:raise ValueError('source changed: '+name)
    header=ROOT/'games/warcraft-3/game/tests/retail_interrupt209_scene.h'
    if header.read_text()!=scene(ROOT/'tools/frida/research/interrupt209_probe.j'):
        raise ValueError('engine producer differs from original gameplay')
    observed=[];public=[]
    for name,pin in spec['captures'].items():
        path=archive/name;raw=path.read_bytes()
        if len(raw)!=pin['bytes'] or hashlib.sha256(raw).hexdigest()!=pin['sha256']:raise ValueError('capture pin differs')
        rows=[json.loads(l)for l in raw.splitlines()];meta={k:v for k,v in rows[0].items()if k!='pid'}
        if meta!=pin['metadata'] or not meta['owned'] or meta['sha256']!=SHA:raise ValueError('provenance differs')
        if any(r.get('type')=='error' or r['event']in('error','trace-failed')for r in rows):raise ValueError('failed capture')
        ends=[r for r in rows if r['event']=='preload-file']
        if len(ends)!=1 or not ends[0].get('complete') or ends[0]['markers']!=152:raise ValueError('incomplete probe')
        preload=path.with_name(path.stem+'-preload.txt')
        if digest(preload)!=ends[0]['sha256']:raise ValueError('preload pin differs')
        markers=re.findall(r'call Preload\( "(I209 [^"\r\n]*)" \)',preload.read_text())
        if len(markers)!=152:raise ValueError('missing public markers')
        public.append(markers)
        if meta['mode']=='observe':
            end=[r for r in rows if r['event']=='trace-end']
            if len(end)!=1 or not end[0]['installed']:raise ValueError('observer did not finish')
            if [r['value']for r in rows if r['event']=='marker']!=markers:raise ValueError('stored public timeline differs')
            observed.append(normalize(rows))
        elif meta['mode']!='control' or any(r['event']not in('metadata','loading-key','controller-end','control-start-file','preload-file')for r in rows):
            raise ValueError('control was instrumented')
    if len(observed)!=2 or any(r!=expected for r in observed) or any(m!=public[0]for m in public):
        raise ValueError('original repeats/control differ')
    return {'observations':2,'controls':1,'rows':2768,'public_markers':152}


def engine_total(text,returncode):
    match=re.search(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\)(.*?) ===',text)
    if not match or returncode or match[1]!=match[2] or int(match[1])<100 or int(match[3])!=2 or match[4] or 'FAIL ['in text:
        raise ValueError('failed/empty actual engine run')
    return int(match[1])


def verify(args):
    spec=json.loads(args.fixture.read_text())
    if digest(args.binary)!=SHA:raise ValueError('unsupported original binary')
    evidence=original(spec,args.archive);editions={}
    for edition in ('roc','tft'):
        log=args.report.with_name(args.report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
        if log.exists() or junit.exists():raise ValueError('engine report must be fresh')
        command=[str(args.test_binary.resolve()),'-data',str(args.data.resolve())]
        if edition=='tft':command+=['-tft']
        command+=['+dedicated','1','+test','wc3_interrupt.*']
        with log.open('w')as out:
            child=subprocess.run(command,cwd=ROOT,env=dict(os.environ,TEST_JUNIT=str(junit)),stdout=out,stderr=subprocess.STDOUT,timeout=300)
        total=engine_total(log.read_text(),child.returncode);suite=ET.parse(junit).getroot()
        if (suite.attrib['tests']!='2' or any(suite.attrib[k]!='0'for k in('failures','errors','skipped')) or
            int(suite.attrib['assertions'])!=total or any(list(row)for row in suite.findall('testcase')) or
            {row.attrib['name']for row in suite.findall('testcase')}!=set(TESTS)):
            raise ValueError('engine identities/counters differ')
        editions[edition]={'assertions':total,'log_sha256':digest(log),'junit_sha256':digest(junit),'command':command}
    return {'passed':True,'status':'verified','original':evidence,'engine_editions':2,'engine':editions,
            'removed_units':4,'completed_members':2,'destroyed_wrappers':196,'owner_intervals':400,
            'binary_sha256':SHA,'fixture_sha256':digest(args.fixture),'test_binary_sha256':digest(args.test_binary),
            'game_library_sha256':digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'),'exclusions':spec['exclusions']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=FIXTURE);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');args=p.parse_args()
    if args.report.exists():p.error('report must be fresh')
    result=verify(args);args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
