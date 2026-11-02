#!/usr/bin/env python3
"""Verify actual point Attack admission, raw goals and whole mover lifetimes."""
import argparse,ctypes,gzip,json,re,sys
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,original_bytes
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
import target371_trace as trace
SPEC=ROOT/'tools/ghidra/fixtures/retail-target371-1.27.json'
TESTS=['wc3_movement.target371_public_attack_point_retains_click_and_admits_physical_owner_synchronously',
       'wc3_attack_orders.*','wc3_movement.target223*']


def public(text):return re.findall(r'call Preload\( "(T371 [^"\r\n]*)" \)',text)


def runtime(groups,spec,engine=None):
    if set(groups)!=set(trace.GOALS):raise ValueError('missing command producer variant')
    records=markers=0;arithmetic={}
    for variant in trace.GOALS:
        cases=groups[variant];expected=spec['scenes'][variant]
        if len(cases)!=3:raise ValueError('missing independent repeat/control')
        for index,case in enumerate(cases):
            rows=trace.validate(case['rows'],variant);meta=rows[0]
            mode='control'if index==2 else'observe'
            if(meta.get('mode')!=mode or meta.get('launcher')!='owned-wine-popen'or
               meta.get('env')not in('B','C')or meta.get('display')!={'B':':98','C':':99'}[meta['env']]or
               meta.get('remote')!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']]or
               meta.get('prefix')!='T371 'or meta.get('input')or meta.get('frida')!='17.18.0'or
               meta.get('source_sha256')!=expected['sources'][index]or
               meta['source_sha256']['map']!=expected['map_sha256']):raise ValueError('capture provenance differs')
            stream=public(case['preload'])
            if stream!=expected['public_markers']:raise ValueError('unhooked/public stream differs')
            footer=dict(event='preload-file',sha256=digest(case['preload'].encode()),markers=trace.MARKERS[variant],complete=True)
            if rows[-1]!=footer:raise ValueError('incomplete capture/preload binding')
            if mode=='observe':
                if trace.project(rows,variant)!=expected['projection']:raise ValueError('whole command lifetime differs')
                observed=[r['value']for r in rows if r.get('event')=='marker']
                if observed!=stream:raise ValueError('observed public stream differs from preload')
                if engine:arithmetic[variant+'-'+str(index)]=trace.arithmetic(rows,engine)
        records+=len(expected['projection']);markers+=3*trace.MARKERS[variant]
    if records!=3422:raise ValueError('whole lifetime coverage changed')
    return dict(observed_captures=4,controls=2,public_markers=markers,native_records=records,arithmetic=arithmetic)


def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='TARGET-01.2'or spec.get('game_sha256')!=SHA or
       spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=3216):raise ValueError('wrong task/tests/original guard inventory')
    for name,expected in spec['pins'].items():
        if digest((ROOT/name).read_bytes())!=expected:raise ValueError('source changed '+name)
    packed=SPEC.with_suffix('.json.gz').read_bytes()
    if digest(packed)!=spec['bundle_sha256']:raise ValueError('frozen capture bundle changed')
    groups=json.loads(gzip.decompress(packed));runtime(groups,spec);return groups


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());groups=validate(spec);original_bytes(a.binary,spec['instructions'])
    arithmetic=runtime(groups,spec,ctypes.CDLL(str(a.engine_library.resolve())))
    import wc3_pathfinding_engine_tests as runner
    engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=3216,
                **arithmetic,engine=engine,scope=spec['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
