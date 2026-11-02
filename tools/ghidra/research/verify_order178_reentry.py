#!/usr/bin/env python3
"""Check full frozen nested order/death/removal streams and archived provenance."""
import argparse,gzip,json,re
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_order177_subscribers import check_capture
from verify_attack173_orders import module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-03.2-expected.json.gz')
SHA='2390bc63184c1a191e197700f345de6e778d7f312305eea3feb2911b6aeb94e7'

def phase(rows,number):
    return [s for s in rows if re.search(r'phase='+str(number)+r'\b',s)]

def entries(rows):
    return [re.search(r'enter trig=(\S+)',s)[1]for s in rows if 'enter trig='in s]

def claims(data):
    errors=[];live=data['live']
    forward=live['controls']['forward'];nested=live['nested']['control'];payload=live['payload']['control']
    streams=[('stop',phase(forward,6),['P1','R1','RI','R2']),
        ('remove',phase(forward,7),['P1','K1','K2','K3']),
        ('kill',phase(forward,8),['P1','L1','LD','L2','L3']),
        ('remove-nested',phase(nested,5),['P2','M1','M2']),
        ('destroy-nested',phase(nested,6),['P2','X1','P2','X1']),
        ('three-level',phase(nested,8),['P2','Y1','P2','Y1','P2','Y1','Y2','Y2','Y2']),
        ('player-stop',phase(payload,1),['Pa','Pi','Pb','Z1']),
        ('player-remove',phase(payload,2),['Pa','Pb','Q1','Pi','Pi'])]
    for name,rows,want in streams:
        if entries(rows)!=want:errors.append((name,'delivery'))
    for rows,operation,trigger in [(phase(forward,6),'stop','RI'),(phase(forward,8),'kill','LD')]:
        start=[i for i,s in enumerate(rows)if 'op '+operation+' begin'in s]
        end=[i for i,s in enumerate(rows)if 'op '+operation+' end'in s]
        action=[i for i,s in enumerate(rows)if 'enter trig='+trigger+' 'in s]
        if len(start)!=1 or len(end)!=1 or len(action)!=1 or not start[0]<action[0]<end[0]:
            errors.append((operation,'synchronous'))
    for rows,trigger,point in [(phase(forward,6),'R2','-1700.000'),(phase(payload,1),'Pb','-1936.000'),
                               (phase(payload,1),'Z1','-1936.000')]:
        match=[s for s in rows if 'enter trig='+trigger+' 'in s]
        if len(match)!=1 or 'ord=851986 'not in match[0]or 'px='+point+' 'not in match[0]or ':0 eval='not in match[0]:
            errors.append((trigger,'outer-payload'))
    for rows,trigger in [(phase(forward,6),'RI'),(phase(payload,1),'Pi')]:
        match=[s for s in rows if 'enter trig='+trigger+' 'in s]
        if len(match)!=1 or 'ord=851972 'not in match[0]or ':851972 eval='not in match[0]:errors.append((trigger,'stop-head'))
    kill=phase(forward,8)
    for trigger in('LD','L2','L3'):
        match=[s for s in kill if 'enter trig='+trigger+' 'in s]
        if len(match)!=1 or ':0.000:0 eval='not in match[0]:errors.append((trigger,'corpse'))
    remove=phase(nested,5)
    match=[s for s in remove if 'enter trig=M2 'in s]
    if len(match)!=1 or ':420.000:851986 eval='not in match[0]:errors.append('suspended-head')
    for part in('nested','payload'):
        if not live[part]['repeat_equal']:errors.append((part,'repeat'))
        if not live[part]['observer_equals_control']:errors.append((part,'control'))
    if not all(live['observer_equals_control'].values()):errors.append('first-control')
    if not live['repeat']['forward-observe-1 vs 2 (CUnit destructor rows excluded)']:errors.append('first-repeat')
    return errors

def verify(archive):
    raw=gzip.decompress(EXPECTED.read_bytes());assert digest(raw)==SHA
    frozen=json.loads(raw)
    builder=module('order178_expected',Path('tools/frida/research/order03_expected.py'))
    current=builder.build(archive,'ORDER-03.2');assert current==frozen,'reconstructed evidence differs'
    assert current['binary_sha256']==BINARY_SHA and not claims(current),claims(current)
    captures=records=0
    for owner,capture in [('ORDER-03.1',frozen['live']['captures']),
                          ('ORDER-03.2',frozen['live']['nested']['captures']),
                          ('ORDER-03.2',frozen['live']['payload']['captures'])]:
        for name,sha in capture.items():
            if not name.endswith('.jsonl'):continue
            records+=check_capture((archive/owner/'captures'/name).read_bytes(),sha,
                                  'observe'if'-observe-'in name else'control');captures+=1
    return dict(passed=True,status='retail-order-reentry-captures',binary_sha256=BINARY_SHA,
        captures=captures,records=records,observer_free_comparisons=7,matched_repeats=3,
        scope='Full ordered subscriber reentry, immutable per-level payload, nested Stop/death and deferred removal scenes.',
        exclusions=['Prepared archived read-only Frida captures; additional fresh admission correction is separately pinned.',
                    'Physical CUnit destructor timing and trigger sleeps are excluded.',
                    'No archived oracle is counted as fresh instruction execution.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
    result=verify(a.archive);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
