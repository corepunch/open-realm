#!/usr/bin/env python3
"""Verify original structure spell producers, exact physical motion and saved execution."""
import argparse,gzip,hashlib,json,os,re,subprocess,xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-structure279-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS=['wc3_movement.structure279*','wc3_movement.spell185*','wc3_movement.spell269*','wc3_spell.owner267*','wc3_movement.follow278*','wc3_save.rejects_invalid_physical_group_payloads','wc3_save.rejects_prior_save_versions','wc3_save.current_format_uses_current_entity_layout','wc3_save.mixed_sparse_regions_reload_in_owner_order_and_retain_inverse_pixels','wc3_movement.capscope273*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(S279 [^"\r\n]*)" \)',text)
def scene_rows(rows):
    result=[[]for _ in range(6)];scene=-1
    for row in rows:
        if row['event']=='marker':scene=int(re.search(r'scene=(-?\d+)',row['value'])[1])
        if 0<=scene<6:result[scene].append(row)
    return result

def decisions(rows):
    result=[]
    ranges=[0x415a8000]*3+[0x417e8000]*2+[0x41460000]
    destinations=[[0x42400000,0x41000000]]*4+[[0x42440000,0x41100000],[0x42400000,0x41100000]]
    for scene,selected in enumerate(scene_rows(rows)):
        initial=[r for r in selected if r['event']=='spell-initial']
        tasks=[r for r in selected if r['event']=='begin-task']
        setups=[r for r in selected if r['event']=='begin-target']
        if len(initial)!=1 or len(tasks)!=1 or len(setups)!=1:raise ValueError('native producer ownership')
        first,task,setup=initial[0],tasks[0],setups[0]
        if first['widget']!='0x0' or first['target']!=task['target'] or task['range']!=0x43a68000 or setup['range']!=task['range'] or task['persistent'] or setup['persistent']:raise ValueError('native captured range/persistence')
        visits=[r for r in selected if r['event']=='gtick'and r['members']and 'pos'in r['members'][0]]
        if not visits or visits[0]['members'][0]['range']!=ranges[scene] or visits[0]['path']['dest']!=destinations[scene]:raise ValueError('native physical range/destination')
        clocks={r['c']:r['clock'] for r in selected if r['event']=='spell-owner-clock'and r['phase']=='begin'}
        motion=[]
        for visit in visits:
            member=visit['members'][0]
            if len(visit['members'])!=1 or member['range']!=ranges[scene] or visit['c']not in clocks:raise ValueError('physical motion ownership')
            motion.append([visit['c'],*clocks[visit['c']],*member['pos'],*member['vel'],member['range']])
        result.append(motion)
    return result

def validate_runtime(bundle,spec):
    captures=bundle['captures']
    if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('repeats/control required')
    repeated=[]
    for cap in captures:
        rows=cap['rows'];meta,footer=rows[0],rows[-1];public=markers(cap['preload'])
        if meta['task']!='STRUCTURE-SPELL' or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or not footer.get('complete') or footer['markers']!=len(public) or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256'] or any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows):raise ValueError('capture provenance/completion')
        for name in('structure279_capture.py','structure279_observer.js','structure279_make_map.py','structure279_probe.j'):
            if meta['source_sha256'][name]!=spec['source_sha256']['tools/frida/research/'+name]:raise ValueError('captured source differs')
        if public!=spec['public'] or len(public)!=633:raise ValueError('original public movement differs')
        if meta['mode']=='control':
            if any(r['event']in('trace-end','gtick','begin-target','spell-initial')for r in rows):raise ValueError('hooked control')
        else:
            if [r['value']for r in rows if r['event']=='marker'and ' label=start'not in r['value']]!=public:raise ValueError('live public markers differ')
            ends=[r for r in rows if r['event']=='trace-end']
            if len(ends)!=1 or not ends[0]['installed'] or any(k.endswith('-dropped')and v for k,v in ends[0]['counts'].items()):raise ValueError('observer incomplete')
            counts=Counter(r['event']for r in rows)
            for event in('marker','spell-initial','begin-task','begin-target','gtick','gtick-end','spell-owner-clock'):
                if counts[event]!=ends[0]['counts'][event]:raise ValueError('observer incomplete')
            repeated.append(decisions(rows))
    if repeated[0]!=repeated[1] or repeated[0]!=spec['motion']:raise ValueError('repeated physical motion differs')
    route=bundle['route_capture'];rows=route['rows'];meta,footer=rows[0],rows[-1]
    if (meta['mode']!='observe' or meta['sha256']!=SHA or not meta['owned'] or
        meta['source_sha256']['map']!=spec['map_sha256'] or
        meta['source_sha256']['structure279_routes.js']!=spec['source_sha256']['tools/frida/research/structure279_routes.js'] or
        not footer.get('complete') or footer['markers']!=633 or
        hashlib.sha256(route['preload'].encode()).hexdigest()!=footer['sha256'] or
        markers(route['preload'])!=spec['public'] or decisions(rows)!=spec['motion']):
        raise ValueError('route capture differs')
    first=[r for r in scene_rows(rows)[0]if r['event']=='gtick'and r['members']and 'pos'in r['members'][0]]
    final=first[49]['members'][0]['path']
    if (final['cnt']!=[25,3] or final['idx']!=[0,0] or
        final['fine'][:2]!=[0x42320000,0x41180000] or not(final['flags']&0x10000000)):
        raise ValueError('original structure partial route differs')

    detail=bundle['detail_capture'];rows=detail['rows'];meta,footer=rows[0],rows[-1]
    if (meta['mode']!='observe' or meta['task']!='STRUCTURE-SPELL' or meta['sha256']!=SHA or not meta['owned'] or
        meta['source_sha256']['map']!=spec['map_sha256'] or
        meta['source_sha256']['structure279-detail2.js']!=spec['source_sha256']['tools/frida/research/structure279-detail2.js'] or
        not footer.get('complete') or footer['markers']!=633 or
        hashlib.sha256(detail['preload'].encode()).hexdigest()!=footer['sha256'] or
        markers(detail['preload'])!=spec['public'] or decisions(rows)!=spec['motion'] or
        any(r.get('type')=='error' or r['event']=='trace-failed' for r in rows)):
        raise ValueError('detail capture differs')
    builds=[r for r in rows if r['event']=='fine-build-detail' and r['c']==1139]
    if (len(builds)!=1 or builds[0]['result']!=1 or builds[0]['obstruction']!=0 or builds[0]['fineCount']!=25 or
        {k:builds[0]['target'][k] for k in ('box','category','flags')}!=
        dict(box=[6,46,11,51],category=0x010000c2,flags=0x10000001)):
        raise ValueError('captured blue target region differs')
    cells=[{k:r[k]for k in ('p','result','obstruction','targetSeen')}for r in rows
           if r['event']=='fine-cell-detail' and r['c']==1139 and r['p'][0]==46]
    if cells!=[dict(p=[46,y],result=1,obstruction=0,targetSeen=1)for y in range(7,12)]:
        raise ValueError('original target perimeter differs')

    return dict(captures=2,controls=1,public_markers=633,task_cases=6,physical_visits=sum(map(len,repeated[0])))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True);a=ap.parse_args()
    if a.report.exists():ap.error('fresh report required')
    spec=json.loads(SPEC.read_text())
    if spec['game_sha256']!=SHA or spec['engine_patterns']!=TESTS:raise ValueError('contract changed')
    for name,h in spec['source_sha256'].items():
        if digest(ROOT/name)!=h:raise ValueError('source pin '+name)
    if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('damaged frozen captures')
    fixture=ROOT/'games/warcraft-3/game/skills/fixtures/retail_structure279_motion.h'
    literal=[[int(word,0)for word in re.findall(r'0x[0-9a-fA-F]+|[0-9]+',row)]
             for row in re.findall(r'\{([^{}]+)\}',fixture.read_text())]
    if literal!=spec['motion'][0]:raise ValueError('engine fixture differs from original motion')
    evidence=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec);original_bytes(a.binary,spec['instructions'])
    engine=[]
    for tft in(False,True):
        for i,pattern in enumerate(TESTS):
            stem=a.report.with_name(a.report.stem+('-tft'if tft else'-classic')+f'-{i}');xml=stem.with_suffix('.xml');log=stem.with_suffix('.log')
            if xml.exists()or log.exists():raise ValueError('fresh engine outputs required')
            with log.open('w')as out:r=subprocess.run([str(a.test_binary.resolve()),'-data',str(a.data.resolve()),*(['-tft']if tft else[]),'+dedicated','1','+test',pattern],stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml.resolve())},timeout=120)
            x=ET.parse(xml).getroot();cases=list(x.iter('testcase'))
            if r.returncode or not cases or any(c.findall('failure')for c in cases):raise ValueError('engine '+str(log))
            engine.append(dict(tft=tft,pattern=pattern,tests=len(cases),assertions=int(x.get('assertions'))))
    report=dict(status='verified',passed=True,binary_sha256=SHA,instructions=len(spec['instructions']),**evidence,engine=engine,scope=spec['scope']);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
