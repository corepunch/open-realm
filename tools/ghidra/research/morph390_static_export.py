#!/usr/bin/env python3
"""Read-only static export of concrete Morph subclass routing from the shared Ghidra HTTP server.

Uses GET endpoints only (decompile/disassemble/read_memory/xrefs/function-by-address); never
renames, comments or saves. Every exported instruction and data guard is later compared byte for
byte against the retail game.dll by verify_wc3_pathing_morph390.py. Annotations proposed for the
shared database are listed separately (see INTEGRATION/handoff); they are not applied here.
"""
import argparse, hashlib, json, sys, urllib.parse, urllib.request
from pathlib import Path
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FAMILIES = {
    # Payoff390 Stone Form: +a4 removal, +c4/+c8 clock, +408 forward begin, +40c forward callback,
    # +414 reverse begin, +1c rawcode, rebind dispatcher, reverse phase, takeoff, Unit15c owner.
    'stone': dict(phase='payoff390-stone-static-only', table=0x6fafeb3c,
        functions=['6f500230', '6f605d90', '6f607270', '6f50a670', '6f50a590', '6f50a540', '6f517400',
                   '6f562c50', '6f54ff50', '6f56a000', '6f48b890', '6f48ed40'],
        slots=[0xa4, 0xc4, 0xc8, 0x400, 0x404, 0x408, 0x40c, 0x410, 0x414, 0x41c],
        rtti=('.?AVCAbilityStoneForm@@', 0x6fc138e0, 0x6fd1b39c)),
}


def get(base, path, **params):
    return json.load(urllib.request.urlopen(base + path + '?' + urllib.parse.urlencode(params), timeout=120))


def memory(base, address, length):
    return bytes(get(base, '/read_memory', address=f'0x{address:x}', length=length)['data'])


def export(base, family):
    spec = FAMILIES[family]
    out = dict(functions={}, instructions={}, data_guards={})
    for entry in spec['functions']:
        info = get(base, '/get_function_by_address', address='0x' + entry)
        if info.get('entry_point') != entry: raise ValueError('not a function entry ' + entry)
        listing = get(base, '/disassemble_function', address='0x' + entry)['instructions']
        start, end = int(info['body_start'], 16), int(info['body_end'], 16)
        addresses = [int(x['address'], 16) for x in listing]
        if addresses != sorted(addresses) or addresses[0] != start or addresses[-1] > end: raise ValueError('non-linear body ' + entry)
        # The final instruction ends at body_end+1; Ghidra reports the inclusive last byte.
        raw = memory(base, start, end + 1 - start)
        bounds = addresses + [end + 1]
        for i, address in enumerate(addresses):
            out['instructions'][f'{address:x}'] = raw[address - start:bounds[i + 1] - start].hex()
        decompiled = get(base, '/decompile_function', address='0x' + entry)
        refs = get(base, '/get_xrefs_to', address='0x' + entry, limit=200)['references']
        out['functions'][entry] = dict(name=decompiled['name'], end=info['body_end'], decompiled=decompiled['decompiled'],
            references=[dict(site=r['from_address'], type=r['type'], entry=r.get('from_function') or '') for r in refs],
            assembly=''.join(f"{x['address']} {x['instruction']}\n" for x in listing), instruction_count=len(listing))
    table = spec['table']
    for slot in spec['slots']:
        out['data_guards'][f'{table + slot:x}'] = memory(base, table + slot, 4).hex()
    name, col, descriptor = spec['rtti']
    out['data_guards'][f'{table - 4:x}'] = memory(base, table - 4, 4).hex()
    out['data_guards'][f'{col:x}'] = memory(base, col, 20).hex()
    out['data_guards'][f'{descriptor + 8:x}'] = memory(base, descriptor + 8, len(name) + 1).hex()
    if memory(base, descriptor + 8, len(name)).decode('ascii') != name: raise ValueError('RTTI name differs')
    if int.from_bytes(memory(base, table - 4, 4), 'little') != col or int.from_bytes(memory(base, col + 12, 4), 'little') != descriptor:
        raise ValueError('RTTI chain differs')
    out.update(task='SEP-01.2', phase=spec['phase'], binary_sha256=SHA, exporter='read-only Ghidra HTTP GET')
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--family', choices=sorted(FAMILIES), required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--ghidra', default='http://localhost:8089')
    a = p.parse_args()
    if a.output.exists(): p.error('fresh output required')
    result = export(a.ghidra, a.family)
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(len(result['instructions']), 'guards', len(result['functions']), 'functions', len(result['data_guards']), 'data ranges')


if __name__ == '__main__': main()
