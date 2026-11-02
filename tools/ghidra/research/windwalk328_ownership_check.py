#!/usr/bin/env python3
"""Check original BOwk static evidence; never execute or modify the game."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'tools/ghidra/fixtures/research/windwalk328-ownership-original.json'
EXPECTED = {'3fbb80','3fb5d0','48bb00','48ba80','5fb7c0','58ce50','580250','59daa0',
            '63ce40','414b50','414bb0','414060','56db60','599a80','58abc0','58de60',
            '58e110','6314d0','6a74b0','5a7120','581680'}


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
    require(len(calls)==102 and sorted(calls,key=lambda x:x['rva'])==data['direct_calls'], 'call inventory/order')
    require(len(data['vtable_words'])==35, 'slot count')
    for key,s in data['vtable_words'].items():
        require(key==f"{s['rva']:06x}", 'slot identity')
        raw=pe.read(s['rva'],4)
        require(raw.hex()==s['bytes'] and struct.unpack('<I',raw)[0]==BASE+s['target_rva'], 'slot bytes')
    for off,target in {0x12c:0x5a7120,0x178:0x581680,0x33c:0x58e110,0x354:0x63ce40,0x358:0x59daa0}.items():
        require(data['vtable_words'][f'{0xb40014+off:06x}']['target_rva']==target,'BOwk method')
    for r,name in [(0xce6180,'DataA'),(0xce6184,'DataB'),(0xce6188,'DataC')]:
        row=data['column_words'][hex(r)];raw=pe.read(r,4)
        require(raw.hex()==row['bytes'] and struct.unpack('<I',raw)[0]==BASE+int(row['string_rva'],16),'column pointer')
        require(row['string']==name and pe.read(int(row['string_rva'],16),len(name)+1)==name.encode()+b'\0','column name')
    return {'functions':21,'vtable_words':35,'direct_calls':102}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--check',action='store_true',help='read-only verification (always enabled)')
    args=parser.parse_args();data=json.loads(FIXTURE.read_text());binary=args.binary.read_bytes()
    result=check(data,binary)
    def missing(d): d['functions'].pop('58ce50')
    def word(d): d['functions']['58ce50']['bytes']='00'+d['functions']['58ce50']['bytes'][2:]
    def extra(d): d['direct_calls'].append(d['direct_calls'][-1])
    def reorder(d): d['direct_calls'][0],d['direct_calls'][1]=d['direct_calls'][1],d['direct_calls'][0]
    def slot(d): d['vtable_words'][f'{0xb40014+0x178:06x}']['target_rva']+=1
    def column(d): d['column_words']['0xce6180']['string']='DataB'
    for mutate in [missing,word,extra,reorder,slot,column]:
        changed=copy.deepcopy(data);mutate(changed)
        try: check(changed,binary)
        except ValueError: pass
        else: raise ValueError('mutation accepted: '+mutate.__name__)
    print(json.dumps(dict(status='PASS',**result,negative_controls=6,scope='static original bytes only')))

if __name__ == '__main__':
    main()
