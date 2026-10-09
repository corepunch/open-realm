#!/usr/bin/env python3
"""ROUTE-01.2: build the public outside-source fixture map (research tool, new file).

Adapted from map022_make_maps.py (MAP-02.2 research builder): copies the 512-byte wrapped
runtime/Human02Interlude-original.w3m and replaces war3map.w3e (17x17 flat dry vertices,
64x64 fine cells, origin 0,0), war3map.wpm (all 00), war3map.doo (no doodads),
war3mapUnits.doo (no placements) and war3map.j (route012_probe.j).  Shared builders are not
modified.
"""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
NV, NF = 17, 64
DRY_H = 0x2000
WATER_RAW = 8550
OUTPUT = 'rs-route012.txt'


def build_w3e(base):
    if base[:4] != b'W3E!':
        raise ValueError('invalid terrain member')
    ground = struct.unpack_from('<I', base, 13)[0]
    cliff_at = 17 + ground * 4
    cliffs = struct.unpack_from('<I', base, cliff_at)[0]
    size_at = cliff_at + 4 + cliffs * 4
    out = bytearray(base[:size_at] + struct.pack('<IIff', NV, NV, 0.0, 0.0))
    for _ in range(NV * NV):
        out += struct.pack('<HHBBB', DRY_H, WATER_RAW, 0, 0, 2)
    return bytes(out)


def build_wpm():
    return b'MP3W' + struct.pack('<III', 0, NF, NF) + bytes(NF * NF)


def build_doo(base):
    if base[:4] != b'W3do' or struct.unpack_from('<II', base, 4) != (7, 9):
        raise ValueError('expected RoC W3do v7.9 doodad member')
    return base[:12] + struct.pack('<I', 0) + struct.pack('<II', 0, 0)


def build_units(base):
    if base[:4] != b'W3do':
        raise ValueError('invalid placement member')
    return base[:12] + struct.pack('<I', 0)


def build_script(script):
    probe = (HERE / 'route012_probe.j').read_text()
    probe = probe.replace('@OUTPUT@', OUTPUT)
    if '@' in probe:
        raise ValueError('unreplaced probe placeholder')
    for old, new in [('call CreateAllUnits(  )', 'call PathProbeInit()'),
                     ('call InitCustomTriggers(  )', '// ROUTE-01.2: campaign triggers disabled.'),
                     ('call RunInitializationTriggers(  )', '// ROUTE-01.2: probe timer owns the experiment.'),
                     ('call CreateRegions(  )', '// ROUTE-01.2: no campaign regions.'),
                     ('call CreateCameras(  )', '// ROUTE-01.2: no campaign cameras.')]:
        if script.count(old) != 1:
            raise ValueError('unexpected Human02Interlude script: ' + old)
        script = script.replace(old, new)
    script = re.sub(r'call SetCameraBounds\( [^\n]+', 'call SetCameraBounds( 128, 128, 1920, 1920, 128, 1920, 1920, 128 )', script)
    script = re.sub(r'call DefineStartLocation\( ([0-3]), [^\n]+', r'call DefineStartLocation( \1, 1024, 1600 )', script)
    block = re.search(r'^globals\n(.*?)^endglobals\n', probe, re.M | re.S)
    if block is None or script.count('\nendglobals') != 1:
        raise ValueError('expected one globals block per script')
    script = script.replace('\nendglobals', '\n' + block.group(1) + 'endglobals', 1)
    return script.replace('\nendglobals', '\nendglobals\n' + probe[block.end():], 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original 512-byte wrapped campaign map')
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    changed = {}
    with tempfile.TemporaryDirectory(prefix='route012-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            new = {'war3map.w3e': build_w3e, 'war3map.doo': build_doo, 'war3mapUnits.doo': build_units}.get(
                member, lambda d: d)(data)
            if member == 'war3map.wpm':
                new = build_wpm()
            if member == 'war3map.j':
                new = build_script(data.decode('utf-8').replace('\r\n', '\n')).encode('utf-8')
            if new != data:
                changed[member] = hashlib.sha256(new).hexdigest()
                args.output.with_suffix('.' + member.replace('war3map', '').strip('.').replace('.', '_')).write_bytes(new)
            path = root / str(i)
            path.write_bytes(new)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = dict(task='ROUTE-01.2', base_sha256=hashlib.sha256(original).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), members=members,
                  changed_members=changed, preload_output=OUTPUT,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  probe_sha256=hashlib.sha256((HERE / 'route012_probe.j').read_bytes()).hexdigest(),
                  outside_sources=[[-112.0,328.0],[2248.0,328.0],[328.0,-16.0]],
                  fine_origin=[0.0, 0.0], fine_cell_world=32.0,
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('map_sha256', 'changed_members')}, indent=1))


if __name__ == '__main__':
    main()
