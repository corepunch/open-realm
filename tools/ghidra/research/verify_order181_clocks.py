#!/usr/bin/env python3
"""Verify full prepared clock wraps, absolute request restoration and load controls."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_order179_requests import complete_capture
from verify_order177_subscribers import check_capture
from verify_attack173_orders import module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-05.3-expected.json')
SHA='e1f4d270b2d02066a97463db04a90249c9954adc44e926c83f6252619ee89545'

def claims(data):
    errors=[];wrap=data['live']['wrap'];load=data['live']['saveload']
    if not wrap['observer_equals_control']or not load['control']['equal']:errors.append('control')
    if wrap['ordercheck']['pops']!=7880 or load['ordercheck']['pops']!=1525 or any(x['ordercheck']['violations']for x in(wrap,load)):errors.append('pop-order')
    rebase=wrap['rebase']
    if [r[:5]for r in rebase]!=[['rebase-enter','presentation','43960000',0,1],['rebase-leave','presentation','43960000',1,1],['rebase-enter','primary','43960000',0,10],['rebase-leave','primary','43960000',1,10]]:errors.append('rebase-epoch')
    if len(rebase)!=4 or rebase[0][5]or rebase[1][5]:errors.append('independent-presentation')
    else:
        before,after=rebase[2][5],rebase[3][5]
        expected={38:('43960fff','3dfff000'),41:('43960fff','3dfff000'),883:('43961999','3e4cc800'),31:('43962666','3e999800'),26:('43962666','3e999800'),892:('43980000','40800000'),891:('43980000','40800000'),893:('43980000','40800000'),20:('43e10000','43160000')}
        if len(before)!=9 or len(after)!=9 or {r[2]for r in before}!=set(expected):errors.append('rebase-count')
        elif any(a[1:]!=b[1:] or (a[0],b[0])!=expected.get(a[2])for a,b in zip(before,after)):errors.append('rebase-words')
    primary=wrap['near_span_primary'];presentation=wrap['near_span_presentation']
    if primary[-2:]!=[['advance-near-span','4395fffe',0,'3ba3d70a'],['advance-near-span-leave','3ba10000',1,None]]:errors.append('primary-remainder')
    if presentation[-2:]!=[['advance-near-span','4395fee1',0,'3d5d2f1b'],['advance-near-span-leave','3d394000',1,None]]:errors.append('presentation-remainder')
    rows=wrap['around_primary_wrap']
    polls=[r[1:3]for r in rows if r[0]=='execute'and r[1]in('4395ffff','3dfff000')]
    if polls!=[['4395ffff',38],['4395ffff',41],['3dfff000',38],['3dfff000',41]]:errors.append('span-and-remainder-order')
    saved=load['request_save']
    if saved!=[[300,'41f0ffff','3e000000',38,'00020001','a91220','41f05e1f',132],[300,'41f0ffff','3e000000',41,'00020001','a91220','41f05e1f',132]]:errors.append('saved-absolute')
    state=load['load_clock']
    if len(state)!=2:errors.append('load-clock')
    else:
        before,after=state
        for clock in after[1:]:
            expected=('41f05e1f',132,12,'00001000')if clock['id']=='primary'else('41f0af73',0,0,'00000000')
            if (clock['timeW'],clock['serial'],clock['live'],clock['flags'])!=expected or clock['epoch']!=0 or clock['span']!=300:errors.append('restored-clock')
        if before[0]!='game-load-enter'or after[0]!='game-load-leave'or any(c['timeW']!='00000000'for c in before[1:]):errors.append('load-boundary')
    wrappers=load['wrapper_load'];ranges=[r for r in wrappers if r[0]=='a91220'];release=[r for r in wrappers if r[0]=='a8099c']
    if ranges!=[['a91220',['41f0ffff','3e000000',38,'00020001'],None],['a91220',['41f0ffff','3e000000',41,'00020001'],None]]or len(wrappers)!=12:errors.append('restored-polls')
    if release!=[['a8099c',None,['41f05e53','38d1b717',132,'00020000']]]:errors.append('restored-release')
    if load['pre_continuation_vs_post_load_markers']!={'compared':58,'equal':True}or load['post_load_first_marker']!=['RSO5 tick=310 phase=2 el=31.000 beat','41f7f765',132]:errors.append('continuation-markers')
    if load['pre_continuation_vs_post_load_window_rows']!={'ticks':[310,640],'rows':[465,465],'equal':True}or len(load['post_load_window_rows'])!=465:errors.append('continuation-requests')
    return errors

def teardown_claims(rows):
    errors=[];loads=[r for r in rows if r.get('event')=='game-load-enter']
    if len(loads)!=1:return ['load-enter']
    starts=[r for r in rows if r.get('event')=='settle-enter'and r.get('tick')==640 and r['seq']>loads[0]['seq']]
    # Native game-load itself first flushes an empty freshly constructed owner.
    if len(starts)!=1 or starts[0]['by']!='4cefc':errors.append('load-flush')
    old=[r for r in rows if r.get('event')=='settle-enter'and r.get('tick')==640 and r['clocks'].get('primary',{}).get('timeW')=='4281f482']
    if len(old)!=1:return errors+['old-flush']
    end=next((r for r in rows if r.get('event')=='settle-leave'and r['seq']>old[0]['seq']),None)
    if not end or end['clocks']['primary']['timeW']!='00000000':errors.append('outgoing-reset')
    else:
        pops=[r['req']for r in rows if r.get('event')=='execute'and old[0]['seq']<r['seq']<end['seq']]
        if [(r['deadlineW'],r['serial'],r['flags'])for r in pops]!=[('4281ffff',38,'00010001'),('4281ffff',41,'00010001')]:errors.append('outgoing-cancelled-pops')
    return errors

def verify(archive):
    frozen=json.loads(EXPECTED.read_bytes());assert digest(EXPECTED.read_bytes())==SHA
    builder=module('clock181_builder',Path('tools/frida/research/order05_expected.py'))
    current=json.loads(json.dumps(builder.build(archive,'ORDER-05.3')));assert current==frozen,'clock evidence differs from frozen expectations'
    assert frozen['binary_sha256']==BINARY_SHA and not claims(current),claims(current)
    root=archive/'ORDER-05.3/captures';records=0
    for kind in('wrap','saveload'):
        observed=kind+'-observe-1.jsonl';control=kind+'-control-1.jsonl';hashes=current['live'][kind]['captures']
        records+=complete_capture((root/observed).read_bytes(),hashes[observed])
        check_capture((root/control).read_bytes(),hashes[control],'control')
    rows=[json.loads(s)for s in(root/'saveload-observe-1.jsonl').read_bytes().splitlines()]
    assert not teardown_claims(rows),teardown_claims(rows)
    return dict(passed=True,status='retail-pending-request-clocks',binary_sha256=BINARY_SHA,
        observed_captures=2,records=records,observer_free_comparisons=2,pops=9405,violations=0,
        rebased_requests=9,restored_requests=12,continuation_markers=58,continuation_rows=465,
        scope='Complete300-second independent wraps, exact rebase/remainder words, outgoing teardown, UI load of absolute deadline/serial and identical callbacks.',
        exclusions=['Prepared archives verified afresh; no new live launch.',
                    'UI load after epoch1 and public presentation-clock requests are not live witnesses.',
                    'Pause producer remains unobserved; labelled forced-state original cases verify bit0 and identity-sign switch.'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
    r=verify(a.archive);a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
if __name__=='__main__':main()
