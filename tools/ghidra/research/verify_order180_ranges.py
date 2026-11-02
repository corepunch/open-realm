#!/usr/bin/env python3
"""Verify complete prepared range-request callback, rearm and release evidence."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_order179_requests import complete_capture
from verify_order177_subscribers import check_capture
from verify_attack173_orders import module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-05.2-expected.json')
SHA='3899704e3385f83dfbbaa32134c289e0f874431d7a478b79dfae4a7064e841fa'

def claims(data):
    errors=[];live=data['live']['requests'];rows=live['rows']
    if live['repeat_equal']is not True:errors.append('repeat')
    if len(live['observer_equals_control'])!=2 or not all(live['observer_equals_control'].values()):errors.append('control')
    if sorted(x['pops']for x in live['ordercheck'].values())!=[715,717] or any(x['violations']for x in live['ordercheck'].values()):errors.append('pop-order')
    def selected(kind,serial):return [r for r in rows if r[0]==kind and r[8 if kind=='Q' else 7]==serial]
    ra=selected('X',42);rb=selected('X',45)
    active=[r for r in ra+rb if r[4]=='4037fffc' and r[6]=='00000001']
    if len(active)!=2 or any(r[4]!='4037fffc'for r in active):errors.append('tied-peer-poll')
    starts=[r for r in rows if r[0]=='Q'and r[8]in(75,76,77)]
    if ([r[8]for r in starts]!=[75,76,77] or [r[5]for r in starts]!=['4077fffc','403ffffc','403ffffc'] or
        any(r[-1]!='4037fffc'for r in starts)):errors.append('callback-registration')
    def chain(serials,deadlines):
        queued=[selected('Q',serial)for serial in serials];popped=[selected('X',serial)for serial in serials]
        if any(len(r)!=1 for r in queued+popped):return False
        if [r[0][5]for r in queued]!=deadlines or [r[0][4]for r in popped]!=deadlines:return False
        positions=[rows.index(r[0])for r in popped]
        return positions==sorted(positions) and all(r[0][6]=='00000000'for r in popped)
    if not chain((74,79,80),['4038019f','40380342','403804e5']):errors.append('peer-release-chain')
    if not chain((96,98,99),['40666805','406669a8','40666b4b']):errors.append('self-release-chain')
    self_rearm=[r for r in rows if r[0]=='R'and r[7]==48 and r[4]=='406e6662']
    self_release=selected('X',96)
    if len(self_rearm)!=1 or len(self_release)!=1 or rows.index(self_rearm[0])>=rows.index(self_release[0]):errors.append('rearm-before-release')
    for serial,deadline in ((45,'403ffffc'),(48,'406e6662'),(76,'403ffffc')):
        cancelled=[r for r in selected('X',serial)if r[4]==deadline]
        if len(cancelled)!=1 or cancelled[0][6]!='00010001':errors.append('silent-cancel')
    tied=[r[7]for r in rows if r[0]=='X'and r[4]=='4067fffc'and r[6]=='00000001']
    if tied!=[42,77]:errors.append('stable-repeating-ties')
    return errors

def verify(archive):
    raw=EXPECTED.read_bytes();assert digest(raw)==SHA;frozen=json.loads(raw)
    builder=module('range180_builder',Path('tools/frida/research/order05_expected.py'))
    current=builder.build(archive,'ORDER-05.2');assert current==frozen,'range evidence differs from frozen expectations'
    assert frozen['binary_sha256']==BINARY_SHA and not claims(current),claims(current)
    root=archive/'ORDER-05.1/captures';records=0
    for name in('requests-observe-2.jsonl','requests-observe-3.jsonl'):
        records+=complete_capture((root/name).read_bytes(),current['live']['requests']['captures'][name])
    check_capture((root/'requests-control-1.jsonl').read_bytes(),current['live']['requests']['captures']['requests-control-1.jsonl'],'control')
    return dict(passed=True,status='retail-range-request-captures',binary_sha256=BINARY_SHA,
        observed_captures=2,records=records,observer_free_comparisons=2,matched_repeats=1,
        pops=1432,violations=0,normalized_rows=len(current['live']['requests']['rows']),
        scope='Complete public range producers: callback-created registration phase, tied peer suppression, post-callback rearm and three-wrapper release chains.',
        exclusions=['Prepared archives verified afresh; no new live launch is claimed.',
                    'Native private cancelled-node addresses are not engine identities.',
                    'Clock wrap and primary/presentation persistence remain ORDER-05.3.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
    r=verify(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
if __name__=='__main__':main()
