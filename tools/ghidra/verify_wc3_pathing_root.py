#!/usr/bin/env python3
"""Verify original Root producers and fresh game-owned Root/save regressions."""
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
from root211_fixture import normalize,validate,header
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-root211-1.27.json'
PORTABLE=ROOT/'tools/ghidra/fixtures/retail-root211-1.27.jsonl.gz'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def contract(spec):
    if (spec['version'],spec['binary_sha256'],spec['rows'],spec['angular_visits'],spec['public_markers'])!=(1,SHA,4904,57,1068):
        raise ValueError('Root contract differs')
    if digest(PORTABLE)!=spec['portable_sha256']:raise ValueError('portable pin differs')
    raw=gzip.decompress(PORTABLE.read_bytes())
    if hashlib.sha256(raw).hexdigest()!=spec['timeline_sha256']:raise ValueError('timeline pin differs')
    rows=[json.loads(l)for l in raw.splitlines()];validate(rows)
    if (ROOT/'games/warcraft-3/game/tests/retail_root211.h').read_text()!=header(rows):
        raise ValueError('engine Root words differ from original')
    return rows


def original(spec,archive):
    expected=contract(spec)
    for path,pin in spec['archive_pins'].items():
        if digest(archive/path)!=pin:raise ValueError('archive pin differs: '+path)
    for path,pin in spec['sources'].items():
        if digest(ROOT/path)!=pin:raise ValueError('source pin differs: '+path)
    observed=[];public=[]
    for path,cert in spec['captures'].items():
        path=archive/path;raw=path.read_bytes()
        if len(raw)!=cert['bytes'] or hashlib.sha256(raw).hexdigest()!=cert['sha256']:raise ValueError('capture pin differs')
        rows=[json.loads(l)for l in raw.splitlines()];meta={k:v for k,v in rows[0].items()if k!='pid'}
        if meta!=cert['metadata'] or not meta['owned'] or meta['sha256']!=SHA:raise ValueError('provenance differs')
        if any(r.get('type')=='error' or r['event']in ('error','trace-failed')for r in rows):raise ValueError('failed capture')
        end=[r for r in rows if r['event']=='preload-file']
        if len(end)!=1 or not end[0].get('complete') or end[0]['markers']!=1068:raise ValueError('incomplete public probe')
        preload=path.with_name(path.stem+'-preload.txt')
        if digest(preload)!=end[0]['sha256']:raise ValueError('preload pin differs')
        markers=re.findall(r'call Preload\( "(R211 [^"\r\n]*)" \)',preload.read_text());public.append(markers)
        if len(markers)!=1068:raise ValueError('public markers missing')
        if meta['mode']=='observe':
            finish=[r for r in rows if r['event']=='trace-end']
            if len(finish)!=1 or not finish[0]['installed'] or len(rows)!=4910:raise ValueError('observer incomplete')
            if [r['value']for r in rows if r['event']=='marker']!=markers:raise ValueError('stored public timeline differs')
            observed.append(normalize(rows))
        elif meta['mode']!='control' or any(r['event']not in ('metadata','loading-key','controller-end','control-start-file','preload-file')for r in rows):
            raise ValueError('control instrumented')
    if len(observed)!=2 or len(public)!=3 or any(r!=expected for r in observed) or any(m!=public[0]for m in public):
        raise ValueError('complete repeats/control differ')
    return {'observations':2,'controls':1,'rows':4904,'public_markers':1068,'angular_visits':57}


def engine_total(text,returncode):
    match=re.search(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\)(.*?) ===',text)
    if not match or returncode or match[1]!=match[2] or int(match[1])<3000 or match[3]!='19' or match[4] or 'FAIL ['in text:
        raise ValueError('failed/empty engine Root regressions')
    return int(match[1])


def verify(args):
    spec=json.loads(args.fixture.read_text())
    if digest(args.binary)!=SHA:raise ValueError('unsupported original binary')
    evidence=original(spec,args.archive);editions={}
    for edition in ('roc','tft'):
        log=args.report.with_name(args.report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
        if log.exists() or junit.exists():raise ValueError('engine reports must be fresh')
        command=[str(args.test_binary.resolve()),'-data',str(args.data.resolve())]
        if edition=='tft':command+=['-tft']
        command+=['+dedicated','1','+test','wc3_ancient_root.*']
        with log.open('w')as out:
            child=subprocess.run(command,cwd=ROOT,env=dict(os.environ,TEST_JUNIT=str(junit)),stdout=out,stderr=subprocess.STDOUT,timeout=300)
        total=engine_total(log.read_text(),child.returncode);suite=ET.parse(junit).getroot()
        if (suite.attrib['tests']!='19' or any(suite.attrib[k]!='0'for k in ('failures','errors','skipped')) or
            int(suite.attrib['assertions'])!=total or any(list(row)for row in suite.findall('testcase')) or
            {row.attrib['name']for row in suite.findall('testcase')}!=set(spec['engine_tests'])):
            raise ValueError('engine identities/counters differ')
        editions[edition]={'assertions':total,'log_sha256':digest(log),'junit_sha256':digest(junit),'command':command}
    return {'passed':True,'status':'verified','original':evidence,'engine_editions':2,'engine':editions,
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
