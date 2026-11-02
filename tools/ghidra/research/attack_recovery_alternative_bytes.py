#!/usr/bin/env python3
"""Check the frozen recovery predicate's bodies, instructions and data against the original PE."""
import argparse, hashlib, json, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_attack_recovery299_alternative_validation.json'
def check(binary,fixture=FIXTURE):
    spec=json.loads(Path(fixture).read_text()); raw=Path(binary).read_bytes()
    sha=hashlib.sha256(raw).hexdigest(); assert sha==spec['binary']['sha256'], 'original DLL hash differs'
    base=int(spec['binary']['image_base'],16)
    pe=struct.unpack_from('<I',raw,60)[0]
    assert raw[:2]==b'MZ' and raw[pe:pe+4]==b'PE\0\0'
    count=struct.unpack_from('<H',raw,pe+6)[0]; optional=struct.unpack_from('<H',raw,pe+20)[0]
    sections=[]
    for i in range(count):
        offset=pe+24+optional+40*i
        _,rva,size,start=struct.unpack_from('<IIII',raw,offset+8)
        sections.append((rva,size,start))
    guards=[]
    def verify(address,hx,digest=None):
        rva=int(address,16) if isinstance(address,str) else address
        if rva>=base:rva-=base
        expected=bytes.fromhex(hx); assert expected, 'empty original byte guard'
        for section,size,start in sections:
            if section<=rva and rva+len(expected)<=section+size:
                actual=raw[start+rva-section:start+rva-section+len(expected)];break
        else:raise AssertionError('unmapped original RVA '+hex(rva))
        assert actual==expected, 'original bytes differ at '+hex(rva)
        if digest:assert hashlib.sha256(expected).hexdigest()==digest
        guards.append((rva,len(expected)))
    def walk(value):
        if isinstance(value,dict):
            if 'original_bytes_hex' in value:
                address=value.get('function_rva',value.get('address'))
                assert address is not None, 'byte guard missing address'
                verify(address,value['original_bytes_hex'],value.get('original_bytes_sha256'))
            if isinstance(value.get('original_bytes'),str):
                verify(value['address'],value['original_bytes'],value.get('original_bytes_sha256'))
            if 'rva' in value and isinstance(value.get('bytes'),str):
                verify(value['rva'],value['bytes'],value.get('sha256'))
            elif 'address' in value and isinstance(value.get('bytes'),str):
                verify(value['address'],value['bytes'])
            for child in value.values():walk(child)
        elif isinstance(value,list):
            for child in value:walk(child)
    walk(spec)
    assert len(spec['original_function_exports'])==spec['validation']['functions']
    assert len(guards)>spec['validation']['disassembled_instructions'], 'incomplete byte coverage'
    return {'passed':True,'original_sha256':sha,'fixture_sha256':hashlib.sha256(Path(fixture).read_bytes()).hexdigest(),
        'record_checks':len(guards),'unique_address_length_guards':len(set(guards)),
        'scope':'Original bodies, instruction records and labelled static data; no public or C replay parity claim.'}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--fixture',type=Path,default=FIXTURE)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args(); result=check(args.binary,args.fixture)
    if args.report:args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
if __name__=='__main__':main()
