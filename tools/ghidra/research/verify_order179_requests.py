#!/usr/bin/env python3
"""Rebuild complete request captures and verify ordering, repeats and controls."""
import argparse,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_order177_subscribers import check_capture
from verify_attack173_orders import module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-05.1-expected.json')
SHA='083e251b9b64a1b7fdce88c6626d4b32ae6f6dc5ff3078c96ad8db827e76a470'

def complete_capture(raw,sha):
    records=check_capture(raw,sha,'observe');rows=[json.loads(s)for s in raw.splitlines()]
    markers=[r['value']for r in rows if r.get('event')=='marker']
    assert markers and markers[-1].endswith(' complete'),'missing completed probe'
    end=next(r for r in rows if r.get('event')=='trace-end')
    assert not end.get('caps'),'observer cap truncated evidence'
    artifacts=[r for r in rows if r.get('event')=='artifacts']
    assert len(artifacts)==1 and artifacts[0]['errors']==[]
    return records

def claims(data):
    errors=[];live=data['live']['requests']
    if live['repeat_equal']is not True:errors.append('repeat')
    if len(live['observer_equals_control'])!=2 or not all(live['observer_equals_control'].values()):errors.append('control')
    if sorted(x['pops']for x in live['ordercheck'].values())!=[715,717] or any(x['violations']for x in live['ordercheck'].values()):errors.append('pop-order')
    rows=live['rows']
    initial=[r for r in rows if r[0]=='Q' and r[1]==5]
    popped=[r for r in rows if r[0]=='X' and r[1]==5]
    if len(initial)!=4 or len(popped)!=4:errors.append('release-count')
    elif ([r[8]for r in initial]!=[36,37,38,39] or
          [r[7]for r in popped]!=[36,37,38,39] or
          [r[3:7]for r in initial]!=[r[2:6]for r in popped]):errors.append('release-key-order')
    for r in initial:
        if r[5]!='3f000686' or r[6]!='38d1b717' or r[-1]!='3efffff1':errors.append('deadline')
    cancelled=[r for r in rows if r[0]=='X' and r[7]in(41,44)]
    active=[r for r in rows if r[0]=='X' and r[7]in(42,45)]
    if not cancelled or any(r[6]!='00010001'for r in cancelled):errors.append('cancelled')
    if not active or any(r[6]!='00000001'for r in active):errors.append('active')
    # RA's three starts consume the last-freed release blocks first.
    reused=[r[3]for r in rows if r[0]=='Q' and r[1]==10]
    if reused[:4]!=['B3','B2','B1','B0']:errors.append('block-reuse')
    return errors

def verify(archive):
    raw=EXPECTED.read_bytes();assert digest(raw)==SHA;frozen=json.loads(raw)
    builder=module('request179_builder',Path('tools/frida/research/order05_expected.py'))
    current=builder.build(archive,'ORDER-05.1');assert current==frozen,'reconstructed request evidence differs'
    assert frozen['binary_sha256']==BINARY_SHA and not claims(current),claims(current)
    root=archive/'ORDER-05.1/captures';records=0
    for name in ('requests-observe-2.jsonl','requests-observe-3.jsonl'):
        records+=complete_capture((root/name).read_bytes(),current['live']['requests']['captures'][name])
    check_capture((root/'requests-control-1.jsonl').read_bytes(),current['live']['requests']['captures']['requests-control-1.jsonl'],'control')
    # An empty failed launch is retained and hashed, but never admitted as evidence.
    failed=root/'requests-observe-1-FAILED.jsonl';assert failed.read_bytes()==b''
    try:complete_capture(failed.read_bytes(),digest(failed.read_bytes()))
    except AssertionError:pass
    else:raise AssertionError('failed launch was accepted')
    return dict(passed=True,status='retail-agent-request-captures',binary_sha256=BINARY_SHA,
        observed_captures=2,records=records,observer_free_comparisons=2,matched_repeats=1,
        pops=1432,violations=0,excluded_failed=1,
        scope='Complete prepared Frida request lifetimes, actual public release/listener producers, exact deadline/serial and block reuse.',
        exclusions=['Archived captures, not newly launched runs; fresh original execution is verified separately.',
                    'No public presentation-clock request; forced original-code cases cover that helper contract.',
                    'Range-listener engine polling and request save/load integration remain ORDER-05.2/05.3.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
    r=verify(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
if __name__=='__main__':main()
