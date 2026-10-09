#!/usr/bin/env python3
"""MAP-02.2: build file-backed cliff / water-boundary / bridge fixture maps.

Copies runtime/Human02Interlude-original.w3m (512-byte HM3W wrapper + MPQ) and
replaces war3map.w3e (17x17 vertices, 64x64 fine cells), war3map.wpm, war3map.doo
(one LT04 walkable bridge destructable), war3mapUnits.doo (empty) and war3map.j
(MAP-02.2 support probe). Variants:
  authored : WorldEdit-like WPM bytes (dry 40, shallow 08, deep 0a, cliff ca)
  blank    : identical W3E/doo, WPM all 00 (tests whether the loader derives lanes from W3E)
Research tool; does not modify shared builders.
"""
import argparse, hashlib, json, re, struct, subprocess, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TILE = 128.0
NV = 17          # vertices per axis
NF = 64          # fine cells per axis
DRY_H, SHALLOW_H, DEEP_H = 0x2000, 0x2000 - 4 * 64, 0x2000 - 4 * 192
WATER_RAW = 8550  # (8550-0x2000)/4 = 89.5 world units before the map water offset
UNITS = [('hfoo', 'foot'), ('hsor', 'hover'), ('hbot', 'float'), ('nmyr', 'amph'), ('hgry', 'fly')]
POINTS = [('cliff_bottom', 640.0, 1856.0), ('cliff_top', 1408.0, 1856.0), ('dry', 256.0, 192.0),
          ('shore', 448.0, 192.0), ('shallow', 576.0, 192.0), ('deep', 1024.0, 192.0),
          ('bridge_deck', 1024.0, 640.0), ('bridge_shallow', 576.0, 640.0)]
STAGE = (192.0, 1216.0)
BRIDGE = (b'LT04', 1024.0, 640.0)


def vertex_kind(vx, vy):
    """Return (kind, layer). Water band: vertex rows 0..10, columns 4..12."""
    if vy <= 10 and 4 <= vx <= 12:
        return ('shallow' if vx in (4, 5, 11, 12) else 'deep'), 2
    if vy >= 12 and vx >= 8:
        return 'dry', 3
    return 'dry', 2


def build_w3e(base):
    if base[:4] != b'W3E!':
        raise ValueError('invalid terrain member')
    ground = struct.unpack_from('<I', base, 13)[0]
    cliff_at = 17 + ground * 4
    cliffs = struct.unpack_from('<I', base, cliff_at)[0]
    size_at = cliff_at + 4 + cliffs * 4
    out = bytearray(base[:size_at] + struct.pack('<IIff', NV, NV, 0.0, 0.0))
    for vy in range(NV):
        for vx in range(NV):
            kind, layer = vertex_kind(vx, vy)
            height = {'dry': DRY_H, 'shallow': SHALLOW_H, 'deep': DEEP_H}[kind]
            flags = 0x40 if kind != 'dry' else 0
            out += struct.pack('<HHBBB', height, WATER_RAW, flags, 0, layer)
    return bytes(out)


def tile_class(tx, ty):
    corners = [vertex_kind(tx + dx, ty + dy) for dy in (0, 1) for dx in (0, 1)]
    if len({layer for _, layer in corners}) > 1:
        return 'cliff'
    kinds = {k for k, _ in corners}
    if kinds == {'deep'}:
        return 'deep'
    if kinds - {'dry'}:
        return 'shallow'
    return 'dry'


AUTHORED = {'dry': 0x40, 'shallow': 0x08, 'deep': 0x0a, 'cliff': 0xca}


def build_wpm(variant, deck_cells=()):
    cells = bytearray(NF * NF)
    if variant in ('authored', 'deckwalk'):
        for fy in range(NF):
            for fx in range(NF):
                cells[fy * NF + fx] = AUTHORED[tile_class(fx // 4, fy // 4)]
    if variant == 'deckwalk':
        # Clear walking under the observed LT06 deck (footprint 08 minus c2 rails); keep 08.
        for n in deck_cells:
            cells[n] = 0x08
    return b'MP3W' + struct.pack('<III', 0, NF, NF) + bytes(cells)


def build_doo(base, angle=0.0, code=None):
    if base[:4] != b'W3do' or struct.unpack_from('<II', base, 4) != (7, 9):
        raise ValueError('expected RoC W3do v7.9 doodad member')
    default, x, y = BRIDGE
    code = code or default
    record = code + struct.pack('<I3ff3fBBI', 0, x, y, 0.0, angle, 1.0, 1.0, 1.0, 2, 100, 0)
    assert len(record) == 42
    return base[:12] + struct.pack('<I', 1) + record + struct.pack('<II', 0, 0)


def build_units(base):
    if base[:4] != b'W3do':
        raise ValueError('invalid placement member')
    return base[:12] + struct.pack('<I', 0)


def build_script(script, variant, output_name):
    probe = (HERE / 'map022_probe.j').read_text()
    tables = []
    for i, (code, name) in enumerate(UNITS):
        tables.append(f" set udg_Map022Type[{i}]='{code}'")
        tables.append(f' set udg_Map022TypeName[{i}]="{name}"')
    for i, (name, x, y) in enumerate(POINTS):
        tables.append(f' set udg_Map022Point[{i}]="{name}"')
        tables.append(f' set udg_Map022X[{i}]={x:.1f}')
        tables.append(f' set udg_Map022Y[{i}]={y:.1f}')
    for key, value in {'@POINTS@': str(len(POINTS)), '@CASES@': str(len(POINTS) * len(UNITS)),
                       '@STAGE_X@': f'{STAGE[0]:.1f}', '@STAGE_Y@': f'{STAGE[1]:.1f}',
                       '@OUTPUT@': output_name, '@CROSS_TICKS@': '150', '@CROSS_FROM_X@': '192.0',
                       '@CROSS_TO_X@': '1856.0', '@CROSS_Y@': f'{BRIDGE[2]:.1f}', '@VARIANT@': variant, '@TABLES@': '\n'.join(tables)}.items():
        probe = probe.replace(key, value)
    if '@' in probe:
        raise ValueError('unreplaced probe placeholder')
    for old, new in [('call CreateAllUnits(  )', 'call PathProbeInit()'),
                     ('call InitCustomTriggers(  )', '// MAP-02.2: campaign triggers disabled.'),
                     ('call RunInitializationTriggers(  )', '// MAP-02.2: probe timer owns the experiment.'),
                     ('call CreateRegions(  )', '// MAP-02.2: no campaign regions.'),
                     ('call CreateCameras(  )', '// MAP-02.2: no campaign cameras.')]:
        if script.count(old) != 1:
            raise ValueError('unexpected Human02Interlude script: ' + old)
        script = script.replace(old, new)
    script = re.sub(r'call SetCameraBounds\( [^\n]+', 'call SetCameraBounds( 128, 128, 1920, 1920, 128, 1920, 1920, 128 )', script)
    script = re.sub(r'call DefineStartLocation\( ([0-3]), [^\n]+', r'call DefineStartLocation( \1, 192, 1216 )', script)
    block = re.search(r'^globals\n(.*?)^endglobals\n', probe, re.M | re.S)
    if block is None or script.count('\nendglobals') != 1:
        raise ValueError('expected one globals block per script')
    script = script.replace('\nendglobals', '\n' + block.group(1) + 'endglobals', 1)
    return script.replace('\nendglobals', '\nendglobals\n' + probe[block.end():], 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--variant', choices=('authored', 'blank', 'deckwalk'), required=True)
    ap.add_argument('--deck-cells', type=Path, help='JSON fine-cell indices for deckwalk')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--bridge-angle', type=float, default=0.0,
                    help='doo angle in radians; 0 = deck along Y (layout v1), 1.5707964 = deck along X (layout v2)')
    ap.add_argument('--bridge-code', default='LT04', choices=('LT04', 'LT06'),
                    help='LT04 fixedRot 0 (deck along Y); LT06 fixedRot 90 (deck along X)')
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original 512-byte wrapped campaign map')
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    output_name = f'rs-map022-{args.variant}.txt'
    changed = {}
    with tempfile.TemporaryDirectory(prefix='map022-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            new = {'war3map.w3e': build_w3e, 'war3map.doo': lambda d: build_doo(d, args.bridge_angle, args.bridge_code.encode()),
                   'war3mapUnits.doo': build_units}.get(member, lambda d: d)(data)
            if member == 'war3map.wpm':
                new = build_wpm(args.variant, json.loads(args.deck_cells.read_text()) if args.deck_cells else ())
            if member == 'war3map.j':
                new = build_script(data.decode('utf-8').replace('\r\n', '\n'), args.variant, output_name).encode('utf-8')
            if new != data:
                changed[member] = hashlib.sha256(new).hexdigest()
                args.output.with_suffix('.' + member.replace('war3map', '').strip('.').replace('.', '_')).write_bytes(new)
            path = root / str(i)
            path.write_bytes(new)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    layout = {'vertices': [[vertex_kind(vx, vy) for vx in range(NV)] for vy in range(NV)],
              'tiles': [[tile_class(tx, ty) for tx in range(NV - 1)] for ty in range(NV - 1)]}
    result = dict(task='MAP-02.2', variant=args.variant, base_sha256=hashlib.sha256(original).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), members=members,
                  changed_members=changed, preload_output=output_name,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  probe_sha256=hashlib.sha256((HERE / 'map022_probe.j').read_bytes()).hexdigest(),
                  heights=dict(dry=DRY_H, shallow=SHALLOW_H, deep=DEEP_H, water=WATER_RAW),
                  authored_wpm=AUTHORED if args.variant != 'blank' else 'all zero',
                  deck_cells_sha256=hashlib.sha256(args.deck_cells.read_bytes()).hexdigest() if args.deck_cells else None,
                  bridge=dict(code=args.bridge_code, x=BRIDGE[1], y=BRIDGE[2], z=0.0, angle=args.bridge_angle, flags=2, life=100),
                  units=UNITS, points=POINTS, stage=STAGE, layout=layout,
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('variant', 'map_sha256', 'changed_members')}, indent=1))


if __name__ == '__main__':
    main()
