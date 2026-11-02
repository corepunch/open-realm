#!/usr/bin/env python3
"""Check frozen original Stop/replacement evidence and actual game continuations."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
from cancel208_fixture import extract, render, scene
from cancel208_queue_fixture import extract as queue_extract
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-cancel208-1.27.json'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS=['wc3_cancel.public_stop_and_replacement_match_turning_and_saved_suffix',
       'wc3_cancel.waiting_and_active_replacements_retain_survivor_fifo_and_storage']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def words_digest(data):
    return hashlib.sha256(json.dumps(data,separators=(',',':'),sort_keys=True).encode()).hexdigest()


def validate(spec):
    if (spec['version']!=1 or spec['build']['game_sha256']!=SHA or spec['engine_tests']!=TESTS or
        spec['visual_count']!=326 or spec['turn_stops']!=30 or spec['queue_stops']!=492 or
        spec['searches']!=737 or spec['public_markers']!=[87,57] or spec['samples']!=640 or
        len(spec['scenes'])!=2 or any(len(s['captures'])!=2 or s['preload_payload_limit']!=259 for s in spec['scenes'])):
        raise ValueError('cancellation contract changed')
    for name,expected in spec['pins'].items():
        if digest(ROOT/name)!=expected:raise ValueError('repository input differs: '+name)


def markers(path,prefix,count):
    values=re.findall(re.escape(prefix)+r' .*?(?="\s*\))',path.read_text())
    if len(values)!=count or 'label=complete' not in values[-1]:
        raise ValueError('incomplete public output')
    return values


def original(spec,archive):
    validate(spec)
    for name,expected in spec['archive_pins'].items():
        p=(archive/name).resolve()
        if not p.is_relative_to(archive.resolve()) or digest(p)!=expected:
            raise ValueError('original archive input differs: '+name)
    results=[]
    for item in spec['scenes']:
        previous=public=None
        for name in item['captures']+[item['control']]:
            path=archive/name;rows=[json.loads(line)for line in path.read_text().splitlines()]
            meta,footer=rows[0],rows[-1];mode='control'if name==item['control']else'observe'
            if (meta['event']!='metadata' or meta['sha256']!=SHA or not meta['owned'] or
                meta['mode']!=mode or meta['task']!='ORDER-06.3/payoff208' or
                footer['event']!='preload-file' or not footer['complete'] or footer['markers']!=item['markers']):
                raise ValueError('unowned/incomplete original capture')
            for source,expected in meta['source_sha256'].items():
                p=path.parent/item['map'] if source=='map' else path.parent/'sources'/source
                if digest(p)!=expected:raise ValueError('captured source differs: '+source)
            preload=path.with_name(path.stem+'-preload.txt')
            if digest(preload)!=footer['sha256']:raise ValueError('public file differs')
            current=markers(preload,item['prefix'],item['markers'])
            if public is not None and current!=public:raise ValueError('observed/repeated/control output differs')
            public=current
            if mode=='control':
                if any(r['event']in('installed','trace-end','stop-begin')for r in rows):
                    raise ValueError('control installed hooks')
                continue
            trace=rows[-2]
            if trace['event']!='trace-end' or not trace['installed']:raise ValueError('missing observer finish')
            actual=Counter(r['event']for r in rows if r['event']in trace['counts'])
            if dict(actual)!=trace['counts'] or dict(actual)!=item['counts']:
                raise ValueError('missing/dropped original visits')
            # Original PreloadGen serializes at most 259 payload characters.
            # Full eight-unit words still come from the read-only snapshots.
            if [r['value'][:item['preload_payload_limit']]for r in rows if r['event']=='marker']!=public:
                raise ValueError('hook and public samples differ')
            data=extract(path)if item['prefix']=='C208'else queue_extract(path)
            if words_digest(data)!=item['words_sha256'] or (previous is not None and data!=previous):
                raise ValueError('original intermediate state differs')
            if item['prefix']=='C208' and render(path)!=(ROOT/'games/warcraft-3/game/tests/retail_cancel208.h').read_text():
                raise ValueError('engine words differ from original')
            previous=data
        results.append(dict(prefix=item['prefix'],markers=len(public),words_sha256=words_digest(previous)))
    if scene(ROOT/'tools/frida/research/cancel208_probe.j')!=(ROOT/'games/warcraft-3/game/tests/retail_cancel208_scene.h').read_text():
        raise ValueError('engine producer differs from original gameplay')
    return results


def engine_totals(log,code):
    matches=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log)
    if code or len(matches)!=1:raise ValueError('engine run did not complete once')
    passed,total,cases=map(int,matches[0])
    if passed!=total or passed<14000 or cases!=2:raise ValueError('empty/partial/failed cancellation run')
    return passed


def verify(a):
    spec=json.loads(a.fixture.read_text())
    if digest(a.binary)!=SHA or digest(a.binary.with_name('msvcr120.dll'))!=spec['build']['crt_sha256']:
        raise ValueError('retail binary/CRT differs')
    evidence=original(spec,a.archive)
    work=a.report.with_suffix('.work');work.mkdir(parents=True,exist_ok=False)
    editions={}
    for edition in ('classic','tft'):
        log,junit=work/(edition+'.log'),work/(edition+'.xml')
        command=[str(a.test_binary.resolve()),'-data',str(a.data.resolve())]
        if edition=='tft':command+=['-tft']
        command+=['+dedicated','1','+test','wc3_cancel.*']
        with log.open('w')as out:
            child=subprocess.run(command,cwd=ROOT,env=dict(os.environ,TEST_JUNIT=str(junit)),
                                 stdout=out,stderr=subprocess.STDOUT,timeout=300)
        count=engine_totals(log.read_text(),child.returncode);suite=ET.parse(junit).getroot()
        if (suite.attrib['tests']!='2' or suite.attrib['failures']!='0' or suite.attrib['errors']!='0' or
            suite.attrib['skipped']!='0' or int(suite.attrib['assertions'])!=count or
            any(list(row)for row in suite.findall('testcase')) or
            {row.attrib['name']for row in suite.findall('testcase')}!=set(TESTS)):
            raise ValueError('engine identities/counters differ')
        editions[edition]=dict(assertions=count,log_sha256=digest(log),junit_sha256=digest(junit),command=command)
    return dict(passed=True,scenes=evidence,engine_editions=2,engine=editions,
                visual_visits=326,turn_stops=30,queue_stops=492,searches=737,samples=640,
                binary_sha256=SHA,fixture_sha256=digest(a.fixture),test_binary_sha256=digest(a.test_binary),
                game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
                exclusions=spec['exclusions'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=FIXTURE);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('report must be new')
    result=verify(a);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
