#!/usr/bin/env python3
"""Payoff193 duplicate Captain request research map builder (new research file; shared builders untouched).

Copies the 512-byte wrapped runtime/Human02Interlude-original.w3m, instruments war3map.j with
GROUP-03.4.6.2.1.2_probe.j through the existing captain_home `instrument` (imported read-only:
Player(0) becomes a computer, campaign triggers are disabled), appends authored custom unit rows
(war3map.w3u), adds war3map.w3a with three Drunken Haze clones that only carry an Attacks
Prevented mask, and packs the AI script as Scripts\\wc3_captain_probe.ai.  Run from the repository
root (instrument reads the random fixture relative to the working directory).
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TASK = 'captain193'
from target021_make_map import build_w3e, build_wpm, build_doo, build_units, NF
spec = importlib.util.spec_from_file_location('mwpm', HERE.parent / 'make_wc3_pathfinding_map.py')
mwpm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mwpm)

INT, REAL, UNREAL, STRING = 0, 1, 2, 3
MELEE = [(b'ua1r', INT, 90), (b'uacq', UNREAL, 500.0)]
MISSILE = [(b'ua1w', STRING, 'missile'), (b'ua1r', INT, 400), (b'uacq', UNREAL, 1000.0)]
SPECIAL = [(b'ua1g', STRING, 'tree'), (b'ua1r', INT, 200), (b'uacq', UNREAL, 1000.0)]
UNITS = [
    (b'hfoo', b'hRA0', MELEE), (b'hfoo', b'hRA1', MELEE), (b'hfoo', b'hRA2', MISSILE),
    (b'hfoo', b'hRA3', MISSILE), (b'hfoo', b'hRA4', MISSILE), (b'hfoo', b'hRA5', MELEE),
    (b'hfoo', b'hRA6', MELEE), (b'hfoo', b'hRA7', MELEE), (b'hfoo', b'hRA8', MELEE),
    (b'hrif', b'hRA9', [(b'uacq', UNREAL, 1000.0)]),
    (b'hfoo', b'hRAs', SPECIAL), (b'hfoo', b'hRAt', SPECIAL),
    (b'Hpal', b'HRA0', [(b'ua1r', INT, 100), (b'uacq', UNREAL, 1000.0)]),
    (b'Hpal', b'HRA1', [(b'ua1r', INT, 883), (b'uacq', UNREAL, 1000.0)]),
    (b'Hpal', b'HRA2', [(b'ua1r', INT, 884), (b'uacq', UNREAL, 1000.0)]),
    (b'hfoo', b'hRC0', [(b'uaen', INT, 0)]), (b'Hpal', b'HRC0', [(b'uaen', INT, 0)]),
]
TARGETS = 'air,ground,friend,self,vulnerable,invulnerable,hero,nonhero,organic,mechanical'
HAZE = [(b'ARD1', 1), (b'ARD2', 2), (b'ARD4', 4)]


def value(kind, v):
    if kind == INT:
        return struct.pack('<i', v)
    if kind in (REAL, UNREAL):
        return struct.pack('<f', v)
    return v.encode() + b'\0'


def append_units(data):
    version = struct.unpack_from('<I', data)[0]
    if version != 1:
        raise ValueError('requires version1 unit modifications')
    cursor = 4
    ids = set()
    for table in range(2):
        count_at = cursor
        count = struct.unpack_from('<I', data, cursor)[0]
        cursor += 4
        for _ in range(count):
            old, new, n = struct.unpack_from('<4s4sI', data, cursor)
            ids.add(new)
            cursor += 12
            for _ in range(n):
                field, kind = struct.unpack_from('<4sI', data, cursor)
                cursor += 8
                if kind in (0, 1, 2):
                    cursor += 4
                elif kind == 3:
                    cursor = data.index(b'\0', cursor) + 1
                else:
                    raise ValueError('unsupported unit modification type')
                cursor += 4
    if cursor != len(data) or any(new in ids for _, new, _ in UNITS):
        raise ValueError('trailing data or existing custom identity')
    out = data[:count_at] + struct.pack('<I', count + len(UNITS)) + data[count_at + 4:]
    for base, new, fields in UNITS:
        out += base + new + struct.pack('<I', len(fields))
        for field, kind, v in fields:
            out += field + struct.pack('<I', kind) + value(kind, v) + new
    return out


def ability_table():
    out = struct.pack('<III', 2, 0, len(HAZE))
    for code, mask in HAZE:
        mods = [(b'aher', INT, 0, 0, 0), (b'Nsi1', INT, 1, 1, mask), (b'Nsi2', UNREAL, 1, 2, 0.0),
                (b'Nsi3', UNREAL, 1, 3, 0.0), (b'Nsi4', UNREAL, 1, 4, 0.0), (b'atar', STRING, 1, 0, TARGETS),
                (b'aran', UNREAL, 1, 0, 9999.0), (b'aare', UNREAL, 1, 0, 1.0), (b'amcs', INT, 1, 0, 0),
                (b'acdn', UNREAL, 1, 0, 0.0), (b'adur', UNREAL, 1, 0, 600.0), (b'ahdu', UNREAL, 1, 0, 600.0)]
        out += b'ANdh' + code + struct.pack('<I', len(mods))
        for field, kind, level, column, v in mods:
            out += field + struct.pack('<III', kind, level, column) + value(kind, v) + code
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--group-timed-life', choices=('true', 'false'), default='true')
    ap.add_argument('--no-abilities', action='store_true', help='diagnostic: omit war3map.w3a (haze casts then fail publicly)')
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original 512-byte wrapped campaign map')
    probe = (HERE / (TASK + '_probe.j')).read_text()
    ai = (HERE / (TASK + '_probe.ai')).read_text().replace('@GROUP_TIMED_LIFE@', args.group_timed_life)
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    if 'war3map.w3a' in members or members.count('war3map.w3u') != 1:
        ap.error('unexpected base members')
    changed = {}
    with tempfile.TemporaryDirectory(prefix='grp0346212-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            new = {'war3map.w3e': build_w3e, 'war3map.doo': build_doo, 'war3mapUnits.doo': build_units, 'war3map.shd': lambda _: bytes(NF*NF)}.get(member,lambda d:d)(data)
            if member == 'war3map.wpm': new=build_wpm(False)
            if member == 'war3map.j':
                new = mwpm.instrument(data.decode('utf-8').replace('\r\n', '\n'), probe, 'captain_home').encode('utf-8')
            if member == 'war3map.w3u':
                new = append_units(data)
            if new != data:
                changed[member] = hashlib.sha256(new).hexdigest()
                args.output.with_suffix('.' + member.split('.')[-1]).write_bytes(new)
            path = root / str(i)
            path.write_bytes(new)
            command.extend([str(path), member])
        if not args.no_abilities:
            w3a = root / 'w3a'
            w3a.write_bytes(ability_table())
            command.extend([str(w3a), 'war3map.w3a'])
            changed['war3map.w3a'] = hashlib.sha256(w3a.read_bytes()).hexdigest()
            args.output.with_suffix('.w3a').write_bytes(w3a.read_bytes())
        aip = root / 'ai'
        aip.write_text(ai)
        command.extend([str(aip), 'Scripts\\wc3_captain_probe.ai'])
        args.output.with_suffix('.ai').write_text(ai)
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = dict(task=TASK, base_sha256=hashlib.sha256(original).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), members=members,
                  changed_members=changed, ai_sha256=hashlib.sha256(ai.encode()).hexdigest(),
                  group_timed_life=args.group_timed_life, abilities_member=not args.no_abilities,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  probe_sha256=hashlib.sha256(probe.encode()).hexdigest(),
                  shared_builder_sha256=hashlib.sha256((HERE.parent / 'make_wc3_pathfinding_map.py').read_bytes()).hexdigest(),
                  units=[[b.decode(), n.decode(), [[f.decode(), k, v] for f, k, v in fs]] for b, n, fs in UNITS],
                  abilities=[[c.decode(), m] for c, m in HAZE], targets=TARGETS,
                  preload_output='rs-group0346212.txt',
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('map_sha256', 'changed_members')}, indent=1))


if __name__ == '__main__':
    main()
