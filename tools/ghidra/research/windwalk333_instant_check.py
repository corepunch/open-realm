#!/usr/bin/env python3
"""Verify bounded original AOwk instant admission evidence; no engine parity claim."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require

FIXTURE=Path(__file__).resolve().parents[3]/'tools/ghidra/fixtures/research/windwalk333-instant-original.json'
RANGES={0x580fb0:6,0x435900:8,0x40a500:330,0x3fbb80:372,0x3fb5d0:344,0x40b380:333,0x58ce50:334}
SLOTS={0x30c:0x580fb0,0x34c:0x435900,0x378:0x40a500,0x3c0:0x58ce50}
GATES={0x40a51d:'a900000002',0x40a524:'f6476008',0x40a631:'814e2000010000',0x3fbbd4:'f6476008',0x3fbbda:'8b8684000000',0x3fb611:'f7462000010000',0x3fb61a:'8b8e8c000000',0x40b3fe:'8b8e94000000',0x40b44b:'f7462000010000'}

def check(d,b):
    require(d['schema']==1 and d['image_base']==BASE,'schema/base')
    require(d['original_sha256']==ORIGINAL_SHA==hashlib.sha256(b).hexdigest(),'original binary SHA')
    pe=PE(b);calls=[];instructions={}
    require(set(d['functions'])=={f'{r:06x}' for r in RANGES},'function inventory')
    for r,size in RANGES.items():
        f=d['functions'][f'{r:06x}'];raw=pe.read(r,size)
        require(f['rva']==r and f['size']==size and int(f['body_start'],16)==BASE+r and int(f['body_end'],16)+1==BASE+r+size,'complete body bounds')
        require(f['bytes']==raw.hex() and f['sha256']==hashlib.sha256(raw).hexdigest(),'whole body bytes/hash')
        require(bool(f['decompiled_code'].strip()),'missing decompilation')
        cursor=r
        for i in f['disassembly']:
            a=int(i['address'],16)-BASE;ib=bytes.fromhex(i['bytes'])
            require(a==cursor and 0<len(ib)<=15 and a+len(ib)<=r+size,'instruction coverage')
            require(pe.read(a,len(ib))==ib,'instruction bytes')
            cursor+=len(ib);instructions[a]=i
            if i['mnemonic']=='CALL' and ib[0]==0xe8:
                require(len(ib)==5,'direct call size')
                target=a+5+struct.unpack('<i',ib[1:])[0]
                require(int(i['operands'],16)==BASE+target,'E8 target')
                calls.append(dict(rva=a,callee_rva=target,bytes=ib.hex()))
        require(cursor==r+size,'truncated disassembly')
    require(len(calls)==50 and sorted(calls,key=lambda x:x['rva'])==d['direct_calls'],'complete direct calls')
    require(set(d['vtable_words'])=={f'{0xb40384+o:06x}' for o in SLOTS},'vtable inventory')
    for off,target in SLOTS.items():
        r=0xb40384+off;s=d['vtable_words'][f'{r:06x}'];raw=pe.read(r,4)
        require(s==dict(rva=r,target_rva=target,bytes=raw.hex()) and struct.unpack('<I',raw)[0]==BASE+target,'AOwk vtable slot')
    require(len(d['branch_guards'])==21,'branch inventory')
    require([x['rva'] for x in d['branch_guards']]==sorted(set(x['rva'] for x in d['branch_guards'])),'branch order/duplicates')
    guards={x['rva']:x for x in d['branch_guards']}
    for a,raw in GATES.items():require(a in guards and guards[a]['bytes']==raw,'flag/timing branch')
    for a,g in guards.items():
        i=instructions[a]
        require(g==dict(rva=a,mnemonic=i['mnemonic'],operands=i['operands'],bytes=i['bytes']),'branch annotation')
    return dict(functions=7,vtable_words=4,direct_calls=50,branch_guards=21)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--check',action='store_true');a=p.parse_args()
    d=json.loads(FIXTURE.read_text());b=a.binary.read_bytes();result=check(d,b)
    def missing(x):x['functions'].pop('580fb0')
    def body(x):x['functions']['580fb0']['bytes']='00'+x['functions']['580fb0']['bytes'][2:]
    def bounds(x):x['functions']['40a500']['size']-=1
    def instructions(x):x['functions']['40a500']['disassembly'].pop()
    def call(x):x['direct_calls'][0]['callee_rva']+=1
    def slot(x):x['vtable_words']['b40744']['target_rva']+=1
    def gate(x):x['branch_guards'][0]['bytes']='00'
    def sha(x):x['original_sha256']='0'*64
    for mutate in [missing,body,bounds,instructions,call,slot,gate,sha]:
        bad=copy.deepcopy(d);mutate(bad)
        try:check(bad,b)
        except ValueError:pass
        else:raise ValueError('accepted negative control: '+mutate.__name__)
    print(json.dumps(dict(status='PASS',**result,negative_controls=8,scope='static original evidence only; no engine or Move-preservation claim')))
if __name__=='__main__':main()
