#!/usr/bin/env python3
"""Verify the Polymorph movement profile, additive inverse and retained Move."""
import argparse
from collections import Counter
import gzip
import hashlib
import itertools
import json
from pathlib import Path
import re
import struct
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'tools/ghidra/fixtures/retail-move-profile271-1.27.json'
BUNDLE = FIXTURE.with_suffix('.json.gz')
SOURCES = ['tools/frida/research/move271_'+s for s in ('probe.j', 'observer.js', 'make_map.py')]+[
    'tools/frida/research/rank264_capture.py', 'tools/frida/research/group032_make_map.py',
    'tools/ghidra/research/Move271Evidence.java']
TESTS = ['wc3_movement.profile271*', 'wc3_spell.polymorph*', 'wc3_hero_movement.*',
         'wc3_movement.public_speed*', 'wc3_save.*']

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def markers(text):
    return re.findall(r'call Preload\( "(M271 [^"\r\n]*)" \)', text)

def validate(spec):
    if (spec['version'] != 1 or spec['task'] != 'MOVE-01.1' or spec['game_sha256'] != SHA or
        set(spec['pins']) != set(SOURCES) or spec['engine_tests'] != TESTS or
        len(spec['instructions']) < 650):
        raise ValueError('movement profile contract differs')
    for path, sha in spec['pins'].items():
        if digest(ROOT/path) != sha:
            raise ValueError('source differs: '+path)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('raw captures differ')
    return spec

def verify_live(bundle, spec):
    if len(bundle['captures']) != 3:
        raise ValueError('repeat/control missing')
    sequences = []
    for index, capture in enumerate(bundle['captures']):
        rows, preload = capture['rows'], capture['preload']
        meta, footer = rows[0], rows[-1]
        # The reused runner retains its older task string. The map/probe and
        # exact binary/source pins identify this MOVE-01.1 experiment.
        if (meta['event'] != 'metadata' or meta['task'] != 'GROUP-03.2' or
            meta['sha256'] != SHA or not meta['owned'] or meta['env'] not in ('B', 'C') or
            meta['mode'] != ('control' if index == 2 else 'observe') or
            meta['source_sha256']['map'] != spec['map_sha256'] or
            meta['source_sha256']['rank264_capture.py'] != spec['pins'][SOURCES[3]] or
            footer['event'] != 'preload-file' or not footer['complete'] or footer['markers'] != 10 or
            hashlib.sha256(preload.encode()).hexdigest() != footer['sha256'] or
            markers(preload) != spec['markers'] or
            any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows)):
            raise ValueError('capture provenance/completion differs')
        if index == 2:
            if any('seq' in r or r['event'] == 'trace-end' for r in rows):
                raise ValueError('control was instrumented')
            continue
        if meta['source_sha256']['move271_observer.js'] != spec['pins'][SOURCES[1]]:
            raise ValueError('observer differs')
        events = [r for r in rows if 'seq' in r]
        ends = [r for r in rows if r['event'] == 'trace-end']
        counts = {'module':1, 'marker':10, 'speed-input':16, 'minimum':16, 'maximum':16, 'speed-output':16}
        if (len(ends) != 1 or not ends[0]['readOnly'] or not ends[0]['installed'] or ends[0]['depth'] or
            ends[0]['counts'] != counts or dict(Counter(r['event'] for r in events)) != counts or
            [r['seq'] for r in events] != list(range(1, 76)) or events != spec['sequence'] or
            [r['value'] for r in events if r['event'] == 'marker'] != spec['markers']):
            raise ValueError('ordered speed/profile words differ')
        sequences.append(events)
    if sequences[0] != sequences[1]:
        raise ValueError('retail repeats differ')
    return dict(captures=2, controls=1, public_markers=30, ordered_events=150)

def verify_modifiers(binary):
    """Execute both complete original modifier producers; no game-call hooks.

    Authored profiles are preexisting hash entries. Registration/cache filling
    is outside this oracle. Original hash lookup, subtraction and guards run.
    """
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
    from verify_wc3_pathing_numeric import bits, subtract
    data = binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('original differs')
    pe = struct.unpack_from('<I', data, 60)[0]
    opt = pe+24
    base, size = [struct.unpack_from('<I', data, opt+n)[0] for n in (28, 56)]
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size+4095)&~4095)
    uc.mem_write(base, data[:struct.unpack_from('<I', data, opt+60)[0]])
    for i in range(struct.unpack_from('<H', data, pe+6)[0]):
        at = opt+struct.unpack_from('<H', data, pe+20)[0]+40*i
        va, count, offset = struct.unpack_from('<III', data, at+12)
        if count:
            uc.mem_write(base+va, data[offset:offset+count])
    uc.mem_map(0x10000000, 0x10000)
    uc.mem_map(0x20000000, 0x10000)
    unit, buff, output, bucket, original, form = [0x10000000+n for n in (0, 0x400, 0x800, 0xc00, 0x1000, 0x1400)]
    stack, stop = 0x20008000, 0x30000000
    def write(at, *words):
        uc.mem_write(at, struct.pack('<'+'I'*len(words), *(w&0xffffffff for w in words)))
    def read(at):
        return struct.unpack('<I', uc.mem_read(at, 4))[0]
    def run(entry, self, *args):
        write(stack, stop, *args)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, self)
        uc.emu_start(entry, stop, count=100000)
        if uc.reg_read(UC_X86_REG_EIP) != stop or uc.reg_read(UC_X86_REG_ESP) != stack+4*(len(args)+1):
            raise ValueError('original ABI differs')
    write(0x6fd3c740, bits(-1), 0, bits(1))
    write(0x6fd709f4, bucket)
    write(0x6fd709fc, 0)
    write(bucket, 0, 0, original)
    for row, code, next_row in ((original, 0x68503237, form), (form, 0x6e503237, 0)):
        write(row+0x14, code)
        run(0x6f198420, row+0x14)
        write(row, uc.reg_read(UC_X86_REG_EAX), next_row)
    write(unit+0x30, 0x68503237)
    write(buff+0x30, unit)
    write(buff+0x114, 0x6e503237)
    count = 0
    profiles = [(237, 220), (0, 0), (270.1, 100.2), (-1, 0), (.001, .0002), (1000, 1)]
    for entry, enabled, applied, present, (before, after) in itertools.product(
            (0x6f63a950, 0x6f63ac00), (0, 1), (0, 1), (0, 1), profiles):
        write(original+0x1d4, bits(before))
        write(form+0x1d4, bits(after))
        write(buff+0x20, 0x8000 if applied else 0)
        sentinel = 0x4f123456
        write(output-4, *([sentinel]*13))
        watched = [(unit, 0x100), (buff, 0x118), (original, 0x200), (form, 0x200)]
        prior = [bytes(uc.mem_read(a, n)) for a, n in watched]
        args = [output if present else 0]+([enabled] if entry == 0x6f63a950 else [])
        run(entry, buff, *args)
        expected = subtract(bits(after), bits(before)) if present and applied and (enabled or entry == 0x6f63ac00) else sentinel
        if read(output+0x28) != expected or read(output-4) != sentinel or read(output+0x2c) != sentinel:
            raise ValueError('original modifier guard/arithmetic differs')
        if prior != [bytes(uc.mem_read(a, n)) for a, n in watched]:
            raise ValueError('modifier mutated its inputs')
        count += 1
    return count

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=ROOT/'build/bin/openwarcraft3-tests')
    parser.add_argument('--data', type=Path, default=ROOT/'build/tests')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('fresh report required')
    spec = validate(json.loads(FIXTURE.read_text()))
    original_bytes(args.binary, spec['instructions'])
    live = verify_live(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)
    modifiers = verify_modifiers(args.binary)
    import verify_wc3_pathing_work242 as engine
    previous = engine.TESTS
    try:
        engine.TESTS = TESTS
        checks = engine.run_engine(args.test_binary, args.data, args.report)
    finally:
        engine.TESTS = previous
    result = dict(passed=True, task=spec['task'], **live, modifier_cases=modifiers,
        instructions=len(spec['instructions']), engine=checks,
        fixture_sha256=digest(FIXTURE), test_binary_sha256=digest(args.test_binary),
        game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'), limits=spec['limits'])
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
