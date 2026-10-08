#!/usr/bin/env python3
"""Check complete fresh removal probes, repeat/control streams and causal stacks."""
import argparse,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_order177_subscribers import check_capture
from verify_attack173_orders import digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-03.2-integration178.json')

def read_capture(raw,sha):
    check_capture(raw,sha,'observe')
    rows=[json.loads(s)for s in raw.splitlines()]
    markers=[r['value']for r in rows if r['event']=='marker']
    assert markers and markers[-1].endswith(' complete'),'probe did not complete'
    return rows,markers

def common_markers(markers):
    return [re.sub(r'(op remove end) ev=.*',r'\1',s)for s in markers if 'nested native result='not in s]

def claims(captures,controls):
    errors=[];admission=[c for c in captures if c['scene']=='admission']
    if len(admission)!=2 or admission[0]['markers']!=admission[1]['markers']:errors.append('admission-repeat')
    for capture in captures:
        markers=capture['markers'];rows=capture['producers']
        if common_markers(markers)!=controls[capture['scene']]:errors.append('control')
        if capture['scene']=='admission':
            matching=lambda text:[s for s in markers if text in s]
            result=matching('nested native result=')
            removed=matching('op remove end ev=')
            later=matching('enter trig=M2 ')
            if len(result)!=1 or not result[0].endswith('result=true'):errors.append('native-admission')
            if len(removed)!=1 or ':420.000:0 eval='not in removed[0]:errors.append('canceled-head')
            if len(later)!=1 or ':420.000:851986 eval='not in later[0]:errors.append('suspended-head')
        off=[r for r in rows if r['command']==852056]
        if len(off)!=2:errors.append('off-count');continue
        # Return RVAs, not function entries: first availability retirement, then detach.
        for producer,caller in zip(off,(0x48c992,0x48e898)):
            stack=producer['backtrace']
            if stack[:4]!=[0x6887b5,0x67bd10,0x605af4,caller]:errors.append('causal-stack')
        if 0x69c563 not in off[0]['backtrace']:errors.append('inactive-before-off')
    return errors

def verify(archive):
    frozen=json.loads(EXPECTED.read_bytes());assert frozen['binary_sha256']==BINARY_SHA
    root=archive/'ORDER-03.2/integration178';current=[];records=0
    for capture in frozen['captures']:
        raw=(root/capture['name']).read_bytes();rows,markers=read_capture(raw,capture['sha256'])
        metadata=next(r for r in rows if r['event']=='metadata')
        metadata={k:v for k,v in metadata.items()if k not in('event','pid')}
        assert metadata==capture['metadata'] and len(raw)==capture['bytes']
        assert next(r for r in rows if r['event']=='artifacts')['errors']==[]
        producers=[{k:r[k]for k in('command','tick','phase','backtrace')}for r in rows if r['event']=='immediate-producer']
        assert markers==capture['markers'] and producers==capture['producers']
        current.append(dict(scene=capture['scene'],markers=markers,producers=producers));records+=len(rows)
    controls={}
    for control in frozen['controls']:
        raw=(archive/'ORDER-03.2/captures'/control['name']).read_bytes()
        assert digest(raw)==control['sha256']
        controls[control['scene']]=re.findall(r'call Preload\( "(RSO3 [^"]*)" \)',raw.decode('latin1'))
    assert not claims(current,controls),claims(current,controls)
    for failed in frozen['excluded_captures']:
        raw=(root/failed['name']).read_bytes();assert digest(raw)==failed['sha256']
        try:read_capture(raw,failed['sha256'])
        except AssertionError:pass
        else:raise AssertionError('excluded incomplete capture unexpectedly accepted')
    return dict(passed=True,status='retail-order-removal-suspended',binary_sha256=BINARY_SHA,
        captures=len(current),records=records,admission_repeats=2,causal_stack_pairs=3,
        common_observer_free_comparisons=3,excluded_incomplete=len(frozen['excluded_captures']),
        scope='Fresh public removal/native boolean/current head; repeated complete stream and retirement/detach caller stacks.',
        exclusions=['New native-return marker has two readonly-observer repetitions and instruction proof, but no completed fresh observer-free run.',
                    'Only common markers compare to prepared observer-free controls; new marker is never represented as controlled.',
                    'Physical destruction timing, arbitrary post-removal spells/target/immediate admission and nested sleeps excluded.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
    result=verify(a.archive);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
