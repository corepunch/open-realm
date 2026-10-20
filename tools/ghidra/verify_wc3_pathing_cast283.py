#!/usr/bin/env python3
"""Verify original ordered cast requests, cleanup and class-owned phase timing."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from verify_wc3_pathing_cast282 import normalized, markers, decisions, SHA
from verify_wc3_pathing_target_normalize218 import original_bytes

ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-cast283-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_spell.cast283*','wc3_spell.cast281*','wc3_spell.cast282*',
       'wc3_spell.unit_target_approach_replaces_same_target_cast',
       'wc3_movement.flight280*','wc3_movement.structure279*',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_prior_save_versions',
       'wc3_ability_dispatch.*','wc3_movement.timed_life*','wc3_movement.public_timed_life*']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def requests(rows):
    """Keep every owned dispatcher/arm/virtual/finish word, never host pointers."""
    scene=-1
    owners={}
    for row in rows:
        if row['event']=='marker':scene=int(re.search(r'scene=(-?\d+)',row['value'])[1])
        if row['event']=='channel-phase' and row['phase']=='begin':
            owners[scene]=(row['ability'],row['caster'])
    out=[];scene=-1
    for row in rows:
        if row['event']=='marker':scene=int(re.search(r'scene=(-?\d+)',row['value'])[1])
        if row['event'] not in ('phase-event','phase-arm','phase-method','finish-body','release-active'):continue
        if (row.get('ability'),row.get('caster'))!=owners.get(scene):continue
        fields={k:v for k,v in row.items() if k not in ('ability','caster','arg')}
        # Only v39c takes a success argument. v388/v3a8 have no stack input;
        # observer raw stack words for those slots are return-frame residue.
        if row['event']=='phase-method' and row['slot']==0x39c and row['phase']=='begin':fields['success']=row['arg']
        out.append([scene,fields])
    return out


def validate_runtime(bundle,spec):
    totals=Counter()
    for group in bundle['groups']:
        expected=spec['groups'][group['name']]
        caps=group['captures']
        if len(caps)!=3 or [x['rows'][0]['mode'] for x in caps]!=['observe','observe','control']:
            raise ValueError('repeats/control required')
        contracts=[]
        for cap in caps:
            rows=cap['rows'];meta,footer=rows[0],rows[-1];public=markers(cap['preload'])
            if (meta['task']!='SPELL-PHASES' or meta['sha256']!=SHA or not meta['owned'] or meta['env'] not in ('B','C')
                or meta['source_sha256']['map']!=expected['map_sha256'] or not footer.get('complete')
                or footer['markers']!=len(public) or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256']
                or any(x.get('type')=='error' or x['event']=='trace-failed' for x in rows)):
                raise ValueError('capture provenance/completion')
            for name in expected['captured_sources']:
                if meta['source_sha256'][name]!=spec['source_sha256']['tools/frida/research/'+name]:
                    raise ValueError('captured source differs')
            if normalized(public)!=expected['public']:raise ValueError('original public context differs')
            if meta['mode']=='control':
                if any(r['event'] in ('trace-end','phase-event','channel-phase','effect-body') for r in rows):
                    raise ValueError('hooked control')
            else:
                if [r['value']for r in rows if r['event']=='marker' and ' label=start'not in r['value']]!=public:
                    raise ValueError('live markers differ')
                ends=[r for r in rows if r['event']=='trace-end']
                if len(ends)!=1 or not ends[0]['installed'] or any(k.endswith('-dropped') and v for k,v in ends[0]['counts'].items()):
                    raise ValueError('observer incomplete')
                counts=Counter(r['event']for r in rows)
                for event in ('marker','phase-event','phase-arm','phase-method','finish-body','release-active'):
                    if counts[event]!=ends[0]['counts'].get(event,0):raise ValueError('observer incomplete')
                contracts.append(requests(rows))
        if contracts[0]!=contracts[1] or contracts[0]!=expected['requests']:
            raise ValueError('ordered phase request words differ')
        totals.update(captures=2,controls=1,public_markers=len(expected['public']))
    # The first group's public channel is the prior immutable retail fixture.
    prior=json.loads((ROOT/'tools/ghidra/fixtures/retail-cast282-1.27.json').read_text())
    if spec['groups']['phases']['public']!=prior['public']:
        raise ValueError('prior original public words replaced')
    return dict(totals)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True)
    ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    ap.add_argument('--data',type=Path,default=ROOT/'build/tests')
    ap.add_argument('--report',type=Path,required=True)
    a=ap.parse_args()
    if a.report.exists():ap.error('fresh report required')
    spec=json.loads(SPEC.read_text())
    if spec['game_sha256']!=SHA or spec['engine_patterns']!=TESTS or len(spec['instructions'])!=spec['instruction_count']:
        raise ValueError('contract changed')
    for name,sha in spec['source_sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('source pin '+name)
    if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('damaged frozen captures')
    evidence=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
    original_bytes(a.binary,spec['instructions'])
    engine=[]
    for tft in (False,True):
        for i,pattern in enumerate(TESTS):
            stem=a.report.with_name(a.report.stem+('-tft' if tft else '-classic')+f'-{i}')
            xml,log=stem.with_suffix('.xml'),stem.with_suffix('.log')
            if xml.exists() or log.exists():raise ValueError('fresh engine outputs required')
            with log.open('w') as output:
                result=subprocess.run([str(a.test_binary.resolve()),'-data',str(a.data.resolve()),
                    *(['-tft']if tft else []),'+dedicated','1','+test',pattern],stdout=output,stderr=subprocess.STDOUT,
                    timeout=120,env={**os.environ,'TEST_JUNIT':str(xml.resolve())})
            root=ET.parse(xml).getroot();cases=list(root.iter('testcase'))
            if result.returncode or not cases or any(c.findall('failure')for c in cases):raise ValueError('engine '+str(log))
            engine.append(dict(tft=tft,pattern=pattern,tests=len(cases),assertions=int(root.get('assertions'))))
    report=dict(status='verified',passed=True,binary_sha256=SHA,instructions=len(spec['instructions']),
        **evidence,engine=engine,scope=spec['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
