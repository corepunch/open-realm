#!/usr/bin/env python3
"""Verify original Abur timing bodies and inherited common finish threshold."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
ROOT=Path(__file__).resolve().parents[3] if Path(__file__).parent.name=='research' else Path.cwd()
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require

SIZES={'55d380':341,'54ed20':185,'40b380':333}
SLOTS={0x30c:0x4bca40,0x34c:0x435900,0x378:0x40a500,0x3c0:0x54ed20}
SOURCE='tools/ghidra/fixtures/research/windwalk333-instant-original.json'

def check(d,b,root=ROOT):
    require(d['schema']==1 and d['image_base']==BASE,'schema/base')
    require(d['original_sha256']==ORIGINAL_SHA==hashlib.sha256(b).hexdigest(),'original SHA')
    require(set(d['functions'])=={'55d380','54ed20'},'Morph inventory')
    ref=d['inherited_effect'];require(ref['path']==SOURCE and ref['function']=='40b380','inherited body reference')
    source=(root/SOURCE).read_bytes();require(hashlib.sha256(source).hexdigest()==ref['sha256'],'inherited source hash')
    inherited=json.loads(source)['functions']['40b380']
    functions={**d['functions'],'40b380':inherited};pe=PE(b);allins={};callcount=0
    for key,size in SIZES.items():
        f=functions[key];r=int(key,16);raw=pe.read(r,size)
        require(f['rva']==r and f['size']==size and int(f['body_start'],16)==BASE+r and int(f['body_end'],16)+1==BASE+r+size,'complete body bounds')
        require(f['bytes']==raw.hex() and f['sha256']==hashlib.sha256(raw).hexdigest(),'body bytes/hash')
        require(bool(f['decompiled_code'].strip()),'missing decompilation')
        cursor=r;calls=[]
        for i in f['disassembly']:
            a=int(i['address'],16)-BASE;ib=bytes.fromhex(i['bytes'])
            require(a==cursor and 0<len(ib)<=15 and a+len(ib)<=r+size,'instruction coverage')
            require(pe.read(a,len(ib))==ib,'instruction bytes');cursor+=len(ib);allins[a]=i
            if i['mnemonic']=='CALL' and ib[0]==0xe8:
                require(len(ib)==5,'E8 width');target=a+5+struct.unpack('<i',ib[1:])[0]
                require(int(i['operands'],16)==BASE+target,'decoded E8')
                calls.append(dict(rva=a,callee_rva=target,bytes=ib.hex()))
        require(cursor==r+size,'truncated body')
        if key!='40b380':require(calls==f['direct_calls'],'direct call inventory')
        callcount+=len(calls)
    require(set(d['vtable_words'])=={hex(o) for o in SLOTS},'slot inventory')
    for off,target in SLOTS.items():
        r=0xaf5814+off;raw=pe.read(r,4)
        require(d['vtable_words'][hex(off)]==dict(rva=r,target_rva=target,bytes=raw.hex()) and struct.unpack('<I',raw)[0]==BASE+target,'Burrow slot')
    require(d['epsilon']==dict(rva=0xcd53a0,word=0x3a83126f,bytes='6f12833a') and pe.read(0xcd53a0,4).hex()=='6f12833a','epsilon word')
    guards={0x55d409:'8b8688000000',0x55d41e:'8b8680000000',0x54edc0:'8b8690000000',0x54ed42:'e859ab0100',0x54ed49:'e8f2b10100',0x40b418:'0f869b000000',0x40b449:'776e'}
    for a,raw in guards.items():require(allins[a]['bytes']==raw,'timing instruction '+hex(a))
    require(allins[0x55d417]['operands']=='0x6fd3c744' and allins[0x55d42c]['operands']=='0x6fd3c744','zero setter arguments')
    require(allins[0x40b449]['mnemonic']=='JA' and allins[0x40b418]['mnemonic']=='JBE','strict equality disposition')
    return dict(new_bodies=2,inherited_bodies=1,direct_calls=callcount,vtable_words=4)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--check',action='store_true');p.add_argument('--fixture',type=Path,default=ROOT/'tools/ghidra/fixtures/research/morph337-timing-original.json');a=p.parse_args()
    d=json.loads(a.fixture.read_text());b=a.binary.read_bytes();r=check(d,b)
    def missing(x):x['functions'].pop('54ed20')
    def truncated(x):x['functions']['55d380']['disassembly'].pop()
    def body(x):x['functions']['54ed20']['bytes']='00'+x['functions']['54ed20']['bytes'][2:]
    def call(x):x['functions']['54ed20']['direct_calls'][0]['callee_rva']+=1
    def epsilon(x):x['epsilon']['word']+=1
    def slot(x):x['vtable_words']['0x3c0']['target_rva']+=1
    def inherited(x):x['inherited_effect']['sha256']='0'*64
    for mutate in [missing,truncated,body,call,epsilon,slot,inherited]:
        bad=copy.deepcopy(d);mutate(bad)
        try:check(bad,b)
        except ValueError:pass
        else:raise ValueError('accepted negative control '+mutate.__name__)
    print(json.dumps(dict(status='PASS',**r,negative_controls=7,scope='original static timing evidence only')))
if __name__=='__main__':main()
