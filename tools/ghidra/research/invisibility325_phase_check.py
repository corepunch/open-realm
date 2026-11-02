#!/usr/bin/env python3
"""Check original common-phase reset evidence; no engine parity claim."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
FIXTURES = Path(__file__).resolve().parents[1] / 'fixtures/research'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    args = p.parse_args()
    spec = json.loads((FIXTURES / 'invisibility325-item-phase-original.json').read_text())
    reference = json.loads((FIXTURES / 'invisibility324-public-original.json').read_text())
    assert spec['accepted'] is True and spec['public_records'] == 246
    assert spec['public'] == reference['public_stream']['rows']
    assert hashlib.sha256(('\n'.join(spec['public'])+'\n').encode()).hexdigest() == spec['public_sha256']
    footer = spec['footer']
    assert footer['installed'] and footer['complete'] and not footer['overflow']
    assert footer['dropped'] == footer['pending_queries'] == 0
    assert spec['scoped_records'] == sum(footer['counts'].values()) == 612
    resets = spec['reset_entries']
    assert [(r['rva'], r['unit']['invisibility'], r['fade'], r['caller']) for r in resets] == [
        (0x694b20, 1, 0, 0x643f88), (0x6696e0, 0, 0x40400000, 0x643fa1)] * 2
    assert all(r['tick'] == 49 and r['scene'] == 2 for r in resets)
    image = args.binary.read_bytes()
    assert hashlib.sha256(image).hexdigest() == spec['original_sha256'] == reference['binary_sha256']
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    section_header = pe+24+struct.unpack_from('<H', image, pe+20)[0]
    sections = [struct.unpack_from('<III', image, section_header+i*40+12)
                for i in range(struct.unpack_from('<H', image, pe+6)[0])]
    def raw(rva, size):
        for address, length, offset in sections:
            if address <= rva and rva+size <= address+length:
                return image[offset+rva-address:offset+rva-address+size]
        raise ValueError(hex(rva))
    assert len(spec['function_guards']) == 6
    for guard in spec['function_guards'].values():
        data = bytes.fromhex(guard['bytes'])
        assert hashlib.sha256(data).hexdigest() == guard['sha256']
        assert raw(int(guard['rva'], 16), len(data)) == data
    for site, target in [(0x3fbc37, 0x48bb00), (0x3fb677, 0x48ba80),
                         (0x643f83, 0x694b20), (0x643f9c, 0x6696e0)]:
        data = raw(site, 5)
        assert data[0] == 0xe8 and site+5+struct.unpack_from('<i', data, 1)[0] == target
    assert struct.unpack('<I', raw(0xb67574, 4))[0] == 0x6f643f60
    print(json.dumps(dict(passed=True, public_records=246, observer_records=612,
                          original_functions=6, scope='Native phase reset evidence; no engine parity.')))
if __name__ == '__main__':
    main()
