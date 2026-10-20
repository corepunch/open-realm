#!/usr/bin/env python3
"""Verify original fog publication cadence and a bounded exact reacquisition replay."""
import argparse
import gzip
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ghidra/research'))
from verify_target166_live import fog_rows, hidden_policy
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-fog252-1.27.json'
BUNDLE = FIXTURE.with_suffix('.json.gz')
HEADER = 'games/warcraft-3/game/tests/fixtures/retail_reacquire252.h'
SOURCES = ['tools/frida/research/target03_capture.py', 'tools/frida/research/target021_observer.js',
           'tools/frida/research/target03_vis_observer.js', 'tools/frida/research/target03_probe.j',
           'tools/frida/research/target03r_probe.j', 'tools/frida/research/target021_make_map.py',
           'tools/frida/research/target252_fog_observer.js', 'tools/frida/research/target252_make_map.py',
           'tools/ghidra/research/Work252Evidence.java', HEADER,
           'tools/ghidra/fixtures/research/TARGET-03.2-expected-reacquire.json']
TESTS = ['wc3_movement.target*', 'wc3_game.fow*', 'wc3_fow.*', 'wc3_api.fog*',
         'wc3_jass.timer*', 'wc3_save.*', 'wc3_game.timer*', 'wc3_movement.public_timer*']

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def words_digest(rows):
    return hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest()

def target_rows(rows):
    first = []
    for r in rows:
        if r['event'] == 'marker' and 's=1 ' in r.get('value', ''):
            break
        first.append(r)
    target = next(r['target'] for r in first if r['event'] == 'gtick' and r['target'][0] != 0xffffffff)
    return [[r['c'], *r['members'][0]['pos'], *r['members'][0]['vel']] for r in first
            if r['event'] == 'gtick' and r['count'] == 1 and r['members'][0].get('id') == target]

def header_rows(text, name):
    body = text.split(name + '[][', 1)[1].split('};', 1)[0]
    return [[int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', line)]
            for line in body.splitlines() if line.startswith('    {')]

def verify_runtime(bundle, spec):
    if len(bundle['captures']) != 3:
        raise ValueError('two observations and a control required')
    public = None
    cadence = None
    hidden = 0
    for i, c in enumerate(bundle['captures']):
        rows, preload = c['rows'], c['preload']
        meta, footer = rows[0], rows[-1]
        markers = re.findall(r'call Preload\( "(T3R [^"\r\n]*)" \)', preload)
        if (meta.get('mode') != ('control' if i == 2 else 'observe') or not meta.get('owned') or
            meta.get('sha256') != SHA or meta.get('display') not in (':98', ':99') or
            meta.get('remote') not in ('127.0.0.1:27049', '127.0.0.1:27050') or
            meta['source_sha256']['map'] != spec['map_sha256'] or
            footer.get('event') != 'preload-file' or not footer.get('complete') or
            footer.get('markers') != 1067 or len(markers) != 1067 or
            hashlib.sha256(preload.encode()).hexdigest() != footer['sha256'] or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
            raise ValueError('incomplete, changed or unowned capture')
        if public is not None and public != markers:
            raise ValueError('observer/control public behavior differs')
        public = markers
        if i == 2:
            if any(r['event'] in ('marker', 'gtick', 'trace-end', 'fog-compose') for r in rows):
                raise ValueError('control contains observation')
            continue
        for p in SOURCES[:4] + [SOURCES[5], SOURCES[6]]:
            if meta['source_sha256'][Path(p).name] != spec['pins'][p]:
                raise ValueError('observed source differs')
        ends = [r for r in rows if r['event'] == 'trace-end']
        if len(ends) != 1 or not ends[0]['installed'] or any(k.endswith('-dropped') for k in ends[0]['counts']):
            raise ValueError('incomplete observer')
        if [r['value'] for r in rows if r['event'] == 'marker'] != markers:
            raise ValueError('public markers missing')
        f, t = fog_rows(rows), target_rows(rows)
        if len(f) != 618 or len(t) != 624 or words_digest(f) != spec['follower_sha256'] or words_digest(t) != spec['target_sha256']:
            raise ValueError('raw original trajectories differ')
        composed = [r for r in rows if r['event'] == 'fog-compose']
        actual = [[r['c'], r['period']] for r in composed]
        if len(actual) != 245 or any(w != 0x3eccccce for _, w in actual) or actual != spec['cadence']:
            raise ValueError('fog publication cadence differs')
        if cadence is not None and actual != cadence:
            raise ValueError('fog repeats differ')
        cadence = actual
        if [r['c'] for r in rows if r['event'] == 'fog-destroy'][:1] != [1300] or [c for c, _ in actual if 1300 <= c <= 1305] != [1304]:
            raise ValueError('destroy/publication chronology differs')
        visits = {r[0]: r for r in f}
        if visits[1305][2] != 14 or visits[1306][2] != 0 or visits[1305][1] != 2 or visits[1308][1] != 16:
            raise ValueError('reacquisition bypassed refresh countdown')
        hidden += hidden_policy(rows)
    text = (ROOT / HEADER).read_text()
    if words_digest(header_rows(text, 'target252_reacquire')) != spec['follower_sha256'] or words_digest(header_rows(text, 'target252_target')) != spec['target_sha256']:
        raise ValueError('engine fixture changed original words')
    return dict(captures=2, controls=1, public_markers=3201, fog_compositions=490,
                raw_follower_rows=618, raw_target_rows=624, hidden_visits=hidden, engine_last_counter=1474)

def original_period(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
    from verify_wc3_pathing_numeric import initialize_runtime_scalars
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
        va, n, off = struct.unpack_from('<III', raw, at + 12)
        if n:
            u.mem_write(base + va, raw[off:off+n])
    u.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000
    initialize_runtime_scalars(u, stack, stop)
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]
    before = bytes(u.mem_read(0x6fd69470, 12))
    u.mem_write(0x6fd69474, struct.pack('<I', 0xdeadbeef))
    u.mem_write(stack, struct.pack('<I', stop))
    u.reg_write(UC_X86_REG_ESP, stack)
    for i, reg in enumerate(preserved):
        u.reg_write(reg, 0x12120000+i)
    u.emu_start(0x6f008260, stop, count=10000)
    after = bytes(u.mem_read(0x6fd69470, 12))
    if (u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack + 4 or
        [u.reg_read(r) for r in preserved] != [0x12120000+i for i in range(4)] or
        before[:4] != after[:4] or before[8:] != after[8:] or struct.unpack_from('<I', after, 4)[0] != 0x3eccccce):
        raise ValueError('original period/ABI/canaries differ')
    return 0x3eccccce

def validate(spec):
    if spec['version'] != 1 or spec['task'] != 'TARGET-03.2' or spec['game_sha256'] != SHA or spec['engine_tests'] != TESTS or set(spec['pins']) != set(SOURCES):
        raise ValueError('fog contract differs')
    for p, h in spec['pins'].items():
        if digest(ROOT / p) != h:
            raise ValueError('source changed ' + p)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('capture bundle changed')
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    verify_runtime(bundle, spec)
    return bundle

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    a = p.parse_args()
    if a.report.exists():
        p.error('new report required')
    spec = json.loads(FIXTURE.read_text())
    bundle = validate(spec)
    original_bytes(a.binary, dict(spec['instructions']))
    period = original_period(a.binary)
    import verify_wc3_pathing_work242 as runner
    saved = runner.TESTS
    a.report.parent.mkdir(parents=True, exist_ok=True)
    try:
        runner.TESTS = TESTS
        engine = runner.run_engine(a.test_binary, a.data, a.report)
    finally:
        runner.TESTS = saved
    result = dict(passed=True, **verify_runtime(bundle, spec), period=period, instructions=len(spec['instructions']),
                  engine=engine, binary_sha256=SHA, limits=spec['limits'])
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
