#!/usr/bin/env python3
"""Verify original Morph reverse notification bodies; not an engine parity gate."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require

FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/research/morph336-reverse-original.json'
RANGES = {0x5698a0:198, 0x48ba80:122, 0x691210:79}

def check(data, binary):
    require(data['schema']==1 and data['image_base']==BASE, 'schema/base')
    require(data['original_sha256']==ORIGINAL_SHA==hashlib.sha256(binary).hexdigest(), 'original SHA')
    require(set(data['functions'])=={f'{r:06x}' for r in RANGES}, 'function inventory')
    pe=PE(binary); instructions={}; calls=[]
    for r,size in RANGES.items():
        f=data['functions'][f'{r:06x}']; raw=pe.read(r,size)
        require(f['rva']==r and f['size']==size and int(f['body_start'],16)==BASE+r and int(f['body_end'],16)+1==BASE+r+size, 'complete bounds')
        require(f['bytes']==raw.hex() and f['sha256']==hashlib.sha256(raw).hexdigest(), 'body bytes/hash')
        require(bool(f['decompiled_code'].strip()), 'decompilation missing')
        cursor=r
        for ins in f['disassembly']:
            a=int(ins['address'],16)-BASE; ib=bytes.fromhex(ins['bytes'])
            require(a==cursor and 0<len(ib)<=15 and a+len(ib)<=r+size, 'instruction coverage')
            require(pe.read(a,len(ib))==ib, 'instruction bytes')
            instructions[a]=ins; cursor+=len(ib)
            if ins['mnemonic']=='CALL' and ib[0]==0xe8:
                require(len(ib)==5, 'E8 size')
                target=a+5+struct.unpack('<i',ib[1:])[0]
                require(int(ins['operands'],16)==BASE+target, 'decoded E8 target')
                calls.append(dict(rva=a,callee_rva=target,bytes=ib.hex()))
        require(cursor==r+size, 'truncated body')
    require(len(calls)==13 and sorted(calls,key=lambda c:c['rva'])==data['direct_calls'], 'direct call inventory')
    order=data['notification_order']
    require(order==dict(phase_call=0x569901,subtype_call=0x569911,cast_receiver_slot=0x178,action_target=0x691210), 'notification contract')
    require(instructions[0x569901]['operands']=='0x6f48ba80', 'reverse phase producer')
    require(instructions[0x569911]['bytes']=='ff9010040000', 'subtype v410 dispatch')
    phase=data['functions']['48ba80']['disassembly']
    cast=[int(i['address'],16) for i in phase if i['mnemonic']=='CALL' and i['operands']=='dword ptr [EAX + 0x178]']
    action=[int(i['address'],16) for i in phase if i['mnemonic']=='CALL' and i['operands']=='0x6f691210']
    require(len(cast)==len(action)==1 and cast[0]<action[0], 'CAST before ACTION')
    return dict(functions=3,direct_calls=13)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--check',action='store_true')
    args=p.parse_args(); data=json.loads(FIXTURE.read_text()); binary=args.binary.read_bytes()
    result=check(data,binary)
    def missing(d):d['functions'].pop('5698a0')
    def truncated(d):d['functions']['48ba80']['disassembly'].pop()
    def body(d):d['functions']['691210']['bytes']='00'+d['functions']['691210']['bytes'][2:]
    def target(d):d['direct_calls'][0]['callee_rva']+=1
    def reversed_order(d):d['notification_order']['phase_call']=0x569911
    def wrong_slot(d):d['notification_order']['cast_receiver_slot']=0x174
    def bounds(d):d['functions']['5698a0']['size']-=1
    for mutate in (missing,truncated,body,target,reversed_order,wrong_slot,bounds):
        bad=copy.deepcopy(data); mutate(bad)
        try:check(bad,binary)
        except ValueError:pass
        else:raise ValueError('accepted negative control '+mutate.__name__)
    print(json.dumps(dict(status='PASS',**result,negative_controls=7,scope='original static evidence only')))

if __name__=='__main__':main()
