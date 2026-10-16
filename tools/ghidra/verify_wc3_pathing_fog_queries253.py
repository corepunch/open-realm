#!/usr/bin/env python3
"""Verify retail point/location fog queries without rewriting prior expectations."""
import argparse
import gzip
import hashlib
import json
import re
import struct
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-fog-queries253-1.27.json'
BUNDLE = FIXTURE.with_suffix('.json.gz')
HEADER = 'games/warcraft-3/game/tests/fixtures/retail_fog_queries253.h'
SOURCES = ['tools/frida/research/target03_capture.py', 'tools/frida/research/target021_observer.js',
           'tools/frida/research/target03_vis_observer.js', 'tools/frida/research/target03_probe.j',
           'tools/frida/research/target021_make_map.py', 'tools/frida/research/target253_probe.j',
           'tools/frida/research/target253_make_map.py', 'tools/frida/research/target253_fog_observer.js',
           'tools/ghidra/research/Work253Evidence.java', HEADER]
TESTS = ['wc3_api.fog*', 'wc3_game.fog253*', 'wc3_game.fow_primary*', 'wc3_fow.*',
         'wc3_save.fog*', 'wc3_movement.target252*']

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def public(preload):
    return re.findall(r'call Preload\( "(F253 [^"\r\n]*)" \)', preload)

def normalized(value):
    return re.sub(r' h=-?\d+', '', value)

def verify_runtime(bundle, spec):
    captures = bundle['captures']
    if len(captures) != 3:
        raise ValueError('two observations and an unhooked control required')
    for n, c in enumerate(captures):
        rows, preload = c['rows'], c['preload']
        meta, footer = rows[0], rows[-1]
        markers = public(preload)
        if (meta.get('mode') != ('control' if n == 2 else 'observe') or not meta.get('owned') or
            meta.get('sha256') != SHA or meta.get('display') not in (':98', ':99') or
            meta.get('remote') not in ('127.0.0.1:27049', '127.0.0.1:27050') or
            meta['source_sha256']['map'] != spec['map_sha256'] or
            footer.get('event') != 'preload-file' or not footer.get('complete') or footer.get('markers') != 32 or
            hashlib.sha256(preload.encode()).hexdigest() != footer['sha256'] or markers != spec['markers'] or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
            raise ValueError('changed, incomplete or unowned query capture')
        for path in SOURCES[:5] + ([SOURCES[7]] if n < 2 else []):
            if meta['source_sha256'][Path(path).name] != spec['pins'][path]:
                raise ValueError('captured observer/runner changed')
        if n == 2:
            if any(r['event'] in ('marker', 'fog-compose', 'fog-plane', 'trace-end') for r in rows):
                raise ValueError('control was instrumented')
            continue
        observed = [r for r in rows if r['event'] == 'marker']
        if [r['value'] for r in observed] != markers or [[r['c'], normalized(r['value'])] for r in observed] != spec['sequence']:
            raise ValueError('public query order/deadline changed')
        ends = [r for r in rows if r['event'] == 'trace-end']
        if len(ends) != 1 or not ends[0].get('installed') or any(k.endswith('-dropped') for k in ends[0]['counts']):
            raise ValueError('observer incomplete')
        cadence = [[r['c'], r['period']] for r in rows if r['event'] == 'fog-compose' and r['c'] <= 1157]
        if cadence != spec['cadence'] or any(p != 0x3eccccce for _, p in cadence):
            raise ValueError('fog owner cadence changed')
        creates = [r for r in rows if r['event'] == 'fog-create']
        if (len(creates) != 3 or [r['x'] for r in creates] != [0x43800000, 0x44000000, 0x44400000] or
            any(r['radius'] != 0x43200000 or r['state'] != 4 or r['shared'] or r['after'] != 1 or not r['handle'] for r in creates)):
            raise ValueError('invalid/no-op geometry or modifier creation')
    for marker in spec['markers']:
        m = re.search(r'keep=(\d{3}) stop=(\d{3}) destroy=(\d{3}) locations=(\d{9})$', marker)
        if not m or ''.join(m.groups()[:3]) != m.group(4):
            raise ValueError('coordinate/location disagreement')
    header = (ROOT / HEADER).read_text()
    sequence = [[int(c), json.loads('"'+s+'"')] for c, s in re.findall(r'^    \{(\d+),"([^"\n]*)"\},$', header, re.M)]
    if sequence != spec['sequence']:
        raise ValueError('engine fixture changed original public observations')
    matrix = header.split('fog253_classification[256]={', 1)[1].split('};', 1)[0]
    if [int(x) for x in re.findall(r'\d+', matrix)] != spec['classification'] or len(spec['classification']) != 256:
        raise ValueError('engine fixture changed native classification')
    return dict(captures=2, controls=1, public_markers=96)

def original_classification(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX,
                                  UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
    raw = binary.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('original DLL differs')
    pe = struct.unpack_from('<I', raw, 60)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt + x)[0] for x in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size + 4095) & ~4095)
    for i in range(struct.unpack_from('<H', raw, pe + 6)[0]):
        at = opt + struct.unpack_from('<H', raw, pe + 20)[0] + 40 * i
        va, length, offset = struct.unpack_from('<III', raw, at + 12)
        if length:
            u.mem_write(base + va, raw[offset:offset+length])
    u.mem_map(0x20000000, 0x10000)
    u.mem_map(0x30000000, 4096)
    stack, obj, stop = 0x20008000, 0x20001000, 0x30000000
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]
    results = []
    for flags in range(4):
        u.mem_write(obj, b'\xa5' * 0x78)
        u.mem_write(obj + 0x10, struct.pack('<II', flags & 1, flags >> 1))
        before = bytes(u.mem_read(obj, 0x78))
        for player in range(16):
            for state in range(4):
                visible = (1 << player if state & 2 else 0) | 0xf000
                masked = (0xffff ^ (1 << player) if state & 1 else 0xffff) & 0xfff
                u.mem_write(stack-0x80, b'\xa5' * 0xc0)
                u.mem_write(stack, struct.pack('<4I', stop, visible, masked, 1 << player))
                u.reg_write(UC_X86_REG_ESP, stack)
                u.reg_write(UC_X86_REG_ECX, obj)
                for i, reg in enumerate(preserved):
                    u.reg_write(reg, 0xabcd0001+i)
                u.emu_start(0x6f1e0b80, stop, count=1000)
                if (u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack+16 or
                    [u.reg_read(r) for r in preserved] != [0xabcd0001+i for i in range(4)] or
                    bytes(u.mem_read(obj, 0x78)) != before or bytes(u.mem_read(stack+16, 32)) != b'\xa5'*32):
                    raise ValueError('native query ABI/write guard differs')
                results.append(u.reg_read(UC_X86_REG_EAX))
    return results

def validate(spec):
    if (spec['version'] != 1 or spec['task'] != 'TARGET-03.2' or spec['game_sha256'] != SHA or
        spec['engine_tests'] != TESTS or set(spec['pins']) != set(SOURCES)):
        raise ValueError('query contract differs')
    for p, h in spec['pins'].items():
        if digest(ROOT / p) != h:
            raise ValueError('source changed '+p)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('capture bundle changed')
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    verify_runtime(bundle, spec)
    return bundle

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('new report required')
    spec = json.loads(FIXTURE.read_text())
    bundle = validate(spec)
    original_bytes(a.binary, dict(spec['instructions']))
    actual = original_classification(a.binary)
    if actual != spec['classification']:
        raise ValueError('original query state matrix changed')
    import verify_wc3_pathing_work242 as runner
    saved = runner.TESTS
    a.report.parent.mkdir(parents=True, exist_ok=True)
    try:
        runner.TESTS = TESTS
        engine = runner.run_engine(a.test_binary, a.data, a.report)
    finally:
        runner.TESTS = saved
    result = dict(passed=True, **verify_runtime(bundle, spec), classification_cases=len(actual),
                  instructions=len(spec['instructions']), engine=engine, binary_sha256=SHA, limits=spec['limits'])
    a.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
