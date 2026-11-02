#!/usr/bin/env python3
"""Check original Ghost static evidence; never execute or modify the game."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'tools/ghidra/fixtures/research/ghost330-original.json'
EXPECTED = {'62e400','6311f0','64a280','6257b0','61e7f0','643f60','62e600','631360','427580','43b030','623bd0','617530','635700','493d20'}


def check(data, binary):
    require(data['schema'] == 1 and data['image_base'] == BASE, 'schema')
    require(data['original_sha256'] == ORIGINAL_SHA == hashlib.sha256(binary).hexdigest(), 'original SHA')
    pe = PE(binary)
    require(set(data['functions']) == EXPECTED, 'function inventory')
    calls = []
    for key, f in data['functions'].items():
        r, size = f['rva'], f['size']
        require(key == f'{r:06x}' and int(f['body_start'],16) == BASE+r and int(f['body_end'],16)+1 == BASE+r+size, 'range')
        raw = pe.read(r,size)
        require(raw.hex() == f['bytes'] and hashlib.sha256(raw).hexdigest() == f['sha256'], 'function bytes')
        require(bool(f['decompiled_code']), 'missing decompilation')
        previous = r-1
        for i in f['disassembly']:
            at = int(i['address'],16)-BASE
            require(previous < at < r+size, 'instruction order')
            previous = at
            if i['mnemonic'] == 'CALL' and i['operands'].startswith('0x'):
                raw = pe.read(at,5)
                if raw[0] == 232:
                    dest = at+5+struct.unpack('<i',raw[1:])[0]
                    require(dest == int(i['operands'],16)-BASE, 'call destination')
                    calls.append(dict(rva=at,callee_rva=dest,bytes=raw.hex()))
    require(len(calls)==43 and sorted(calls,key=lambda x:x['rva'])==data['direct_calls'], 'call inventory/order')
    require(len(data['vtable_words'])==24, 'slot count')
    for key,s in data['vtable_words'].items():
        require(key==f"{s['rva']:06x}", 'slot identity')
        raw=pe.read(s['rva'],4)
        require(raw.hex()==s['bytes'] and struct.unpack('<I',raw)[0]==BASE+s['target_rva'], 'slot bytes')
    for off,target in {0xe0:0x62e400,0xe4:0x6311f0,0x12c:0x643f60,0xbc:0x427610,0xc0:0x43b0e0,0xe8:0x427580,0xec:0x43b030}.items():
        require(data['vtable_words'][f'{0xb67778+off:06x}']['target_rva']==target,'Ghost method')
    gate=data['attack_scan_gate']
    require(gate['rva']==0x493d64 and gate['mask']==0x10000000, 'scan bit identity')
    require(gate['bytes']=='f747200000001075ed' and pe.read(gate['rva'],9).hex()==gate['bytes'], 'scan bit branch')
    require(gate['producer_rva']==0x6313b2 and gate['inverse_rva']==0x62e62e, 'bit owner sites')
    require(gate['effect']=='return NULL from493d20 before candidate query; not Binv220 counted scan suspension', 'query versus counter distinction')
    instructions={int(i['address'],16)-BASE:(i['mnemonic'],i['operands']) for f in data['functions'].values() for i in f['disassembly']}
    for address,expected in {
        0x493d64:('TEST','dword ptr [EDI + 0x20], 0x10000000'),
        0x493d6b:('JNZ','0x6f493d5a'),
        0x6313b2:('OR','dword ptr [EAX + 0x20], 0x10000000'),
        0x6313b9:('OR','dword ptr [ESI + 0x20], 0x80'),
        0x62e62e:('AND','dword ptr [EAX + 0x20], 0xefffffff'),
    }.items(): require(instructions.get(address)==expected,'scan/owner instruction guard')
    return {'functions':14,'vtable_words':24,'direct_calls':43}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--check',action='store_true',help='read-only verification (always enabled)')
    args=parser.parse_args();data=json.loads(FIXTURE.read_text());binary=args.binary.read_bytes()
    result=check(data,binary)
    def missing(d): d['functions'].pop('6311f0')
    def word(d): d['functions']['6311f0']['bytes']='00'+d['functions']['6311f0']['bytes'][2:]
    def extra(d): d['direct_calls'].append(d['direct_calls'][-1])
    def reorder(d): d['direct_calls'][0],d['direct_calls'][1]=d['direct_calls'][1],d['direct_calls'][0]
    def slot(d): d['vtable_words'][f'{0xb67778+0xe0:06x}']['target_rva']+=1
    def missing_slot(d): d['vtable_words'].pop(next(iter(d['vtable_words'])))
    def wrong_mask(d): d['attack_scan_gate']['mask']=0x220
    def wrong_owner(d): d['attack_scan_gate']['effect']='stop scan timer'
    for mutate in [missing,word,extra,reorder,slot,missing_slot,wrong_mask,wrong_owner]:
        changed=copy.deepcopy(data);mutate(changed)
        try: check(changed,binary)
        except ValueError: pass
        else: raise ValueError('mutation accepted: '+mutate.__name__)
    print(json.dumps(dict(status='PASS',**result,negative_controls=8,scope='static original bytes only')))

if __name__ == '__main__':
    main()
