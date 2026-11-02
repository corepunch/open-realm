#!/usr/bin/env python3
"""ROUTE-01.1 research oracle: fine and coarse endpoint reconstruction through
the real producers, oblique directions, every footprint class and lane.

Fine: Path_RequestFineRoute 166e90 -> PathFine_BuildRoute 148100 ->
PathFine_Reconstruct 147dc0, footprint classes 0..3 (radius .25/.75/1.25/1.75).
Coarse: Path_RequestAcceleratedRoute 166c30 -> PathAcc_BuildRoute 162cb0 ->
PathAcc_Reconstruct 162a30, the same four classes (stored size 1,1,2,2) and
four lanes (path+88 bits30..31). Route storage is the CLrPath constructor's
own growth-0x80 tables. Read-only entry observers capture the reconstruction
inputs (returned node, parent chain, level/tag bytes, stored size, exact
source/goal words); an independent float32 model must reproduce every output
word. See route01_1_2_harness.py for supplied preconditions.
"""
import argparse
import ctypes
import hashlib
import gzip
import itertools
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from route01_1_2_harness import Retail, fw, wf, f32, model_fine, model_acc  # noqa: E402

SIDE = 64
SOURCE_CELL = (30, 33)
VECTORS = [(sx * a, sy * b) for a, b in ((21, 9), (9, 21), (17, 3), (3, 17), (13, 11), (23, 1), (2, 1), (1, 2))
           for sx in (1, -1) for sy in (1, -1)] + [(0, 0), (1, 0), (0, -1), (1, 1)]
SOURCE_FRACS = [(.125, .875), (f32(.9990234375), .0009765625)]
GOAL_FRACS = [(.25, .75), (f32(.9990234375), .0009765625), (0.0, .5)]
RADII = [.25, .75, 1.25, 1.75]
ALL = (2, 4, 0x40, 0x80)


def build_map(r, name):
    r.clear_map()
    if name == 'wall_gap':
        r.block_fine([(40, y) for y in range(8, 58) if not 30 <= y <= 33])
    elif name == 'lane_mixed':
        r.block_fine([(x, y) for x in range(18, 26) for y in range(38, 46)], masks=(0x40,))
        r.block_fine([(x, y) for x in range(36, 44) for y in range(20, 28)], masks=(0x80,))
        r.block_fine([(x, 26) for x in range(10, 30)], masks=(2, 4))
    elif name == 'enclosed':
        ring = [(x, y) for x in range(44, 57) for y in (44, 56)] + [(x, y) for y in range(44, 57) for x in (44, 56)]
        r.block_fine(ring)
    r.rebuild()



def engine_fixture(payload):
    """Export literal words, never a second engine-side reconstruction model."""
    selected = []
    for kind in ('fine', 'coarse'):
        for row in payload[kind]:
            source = [wf(w) for w in row['source']]
            if source != ([30.125, 33.875] if kind == 'fine' else [15.0625, 16.9375]):
                continue
            if row['goal_frac'] != [0x3e800000, 0x3f400000]:
                continue
            if row['vector'] not in ([21, 9], [-13, -11], [0, 0], [17, 15]):
                continue
            selected.append((kind, row))
    lines = ['/* Generated subset of frozen ROUTE-01.1 original-producer words. */',
             'typedef struct { unsigned map, cls, lane, count, index; bool partial, coarse; uint32_t source[2], goal[2]; uint32_t const (*points)[2]; } retailReconstruction_t;']
    for i, (_, row) in enumerate(selected):
        lines.append('static uint32_t const retail_reconstruct_points_%d[][2]={' % i)
        lines += ['{0x%08xu,0x%08xu},' % tuple(row['words'][k:k + 2]) for k in range(0, len(row['words']), 2)]
        lines.append('};')
    lines.append('static retailReconstruction_t const retail_reconstruction[]={')
    for i, (kind, row) in enumerate(selected):
        lines.append('{%d,%d,%d,%d,%d,%s,%s,{%s},{%s},retail_reconstruct_points_%d},' % (
            ['open', 'wall_gap', 'lane_mixed', 'enclosed'].index(row['map']), row['cls'], row.get('lane', 0),
            row['count'], row['index'], str(bool(row['partial'])).lower(), str(kind == 'coarse').lower(),
            ','.join('0x%08xu' % w for w in row['source']), ','.join('0x%08xu' % w for w in row['goal']), i))
    lines.append('};')
    return '\n'.join(lines) + '\n', len(selected)


def check_saved_evidence(path, binary_sha256):
    saved = json.loads(path.read_text())
    assert saved['unsaved'] is False and saved['binary_sha256'] == binary_sha256
    addresses = {'6f147dc0', '6f162a30', '6f162c40', '6f147da0', '6f162a10', '6f166c30', '6f166e90'}
    assert {row['address'] for row in saved['rows']} == addresses
    assert len(saved['rows']) == 7
    assert all('Payoff140 ROUTE-01.1' in row['comment'] for row in saved['rows'])
    return len(saved['rows'])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--expected', type=Path, help='write (or, if present, compare) frozen expected words')
    parser.add_argument('--engine-library', type=Path, action='append', default=[],
                        help='production probe built from tools/ghidra/wc3_pathing_engine_probe.c (repeat for O0/O2)')
    parser.add_argument('--engine-fixture', type=Path, help='compare the literal Move adapter regression subset')
    parser.add_argument('--ghidra-evidence', type=Path, help='require saved function annotations for this contract')
    args = parser.parse_args()
    r = Retail(args.binary, SIDE)
    engines = []
    for library in args.engine_library:
        e = ctypes.CDLL(str(library.resolve()))

        class Objects(ctypes.Structure):
            _fields_ = [('cells', ctypes.POINTER(ctypes.c_uint8)), ('objects', ctypes.POINTER(ctypes.c_uint32))]
        e.Objects = Objects
        e.pathing_fine_result_words.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Objects), ctypes.POINTER(ctypes.c_uint32)]
        e.pathing_adaptive_route.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint32)]
        engines.append(e)
    engine_cases = dict(fine=0, coarse=0, fine_differences=[], coarse_differences=[])
    captured = []

    def on_fine(uc, address, size, user):
        sp = r.read(r.uc.reg_read(r.R.UC_X86_REG_ESP), 5)
        captured.append(dict(kind='fine', node=sp[1], route=sp[2], source=r.read(sp[3], 2), goal=r.read(sp[4], 2),
                             chain=r.fine_chain(sp[1])))

    def on_acc(uc, address, size, user):
        ecx = r.uc.reg_read(r.R.UC_X86_REG_ECX)
        sp = r.read(r.uc.reg_read(r.R.UC_X86_REG_ESP), 5)
        captured.append(dict(kind='acc', node=sp[1], route=sp[2], size=r.read(ecx + 0x90)[0],
                             source=r.read(sp[3], 2), goal=r.read(sp[4], 2), chain=r.acc_chain(sp[1])))
    r.hook(0x6f147dc0, on_fine)
    r.hook(0x6f162a30, on_acc)

    fine_rows, acc_rows, counts = [], [], dict(fine_model=0, acc_model=0, fine_bypass=0, acc_bypass=0,
                                                 fine_partial=0, acc_partial=0, acc_level_decrements=0,
                                                 acc_levels_seen=set(), sizes_seen=set())
    maps = ['open', 'wall_gap', 'lane_mixed', 'enclosed']
    for name in maps:
        build_map(r, name)
        if name == 'enclosed':
            vectors = [(dx, dy) for dx in (17, 19, 21) for dy in (15, 17, 19)]
            fracs = GOAL_FRACS[:1]
        else:
            vectors, fracs = VECTORS, GOAL_FRACS
        for cls, (dx, dy), frac, sfrac in itertools.product(range(4), vectors, fracs, SOURCE_FRACS):
            source = (f32(SOURCE_CELL[0] + sfrac[0]), f32(SOURCE_CELL[1] + sfrac[1]))
            goal = (f32(SOURCE_CELL[0] + dx + frac[0]), f32(SOURCE_CELL[1] + dy + frac[1]))
            # ---- fine request (lane0 ground query mask 02000000) ----
            r.construct_path()
            r.set_buckets()
            r.uc.mem_write(r.path + 0xb4, struct.pack('<f', RADII[cls]))
            r.write(r.path + 0x9c, 0x02000000)
            r.uc.mem_write(r.points + 0x40, struct.pack('<4f', *source, *goal))
            captured.clear()
            result, popped = r.run(0x6f166e90, r.path, r.points + 0x40, r.points + 0x48)
            assert popped == 8, popped
            words = r.words(0x34)
            obstruction = r.read(r.fine_system + 0xd0)[0]
            index = r.read(r.path + 0x74)[0]
            flags = r.read(r.path + 0x88)[0]
            table = r.table(0x34)
            row = dict(map=name, cls=cls, vector=[dx, dy], goal_frac=[fw(v) for v in frac], source=[fw(v) for v in source],
                       goal=[fw(v) for v in goal], result=result, count=table['count'], capacity=table['capacity'],
                       index=index, obstruction=obstruction, mismatch=bool(flags & 0x10000000), words=words)
            if captured:
                c = captured[0]
                assert len(captured) == 1 and c['kind'] == 'fine'
                assert c['source'] == row['source']
                expected = model_fine(c['chain'], [wf(v) for v in c['source']], [wf(v) for v in c['goal']])
                assert expected == words, (name, cls, dx, dy, frac)
                row['chain_len'] = len(c['chain'])
                row['reconstruct_goal'] = c['goal']
                row['partial'] = c['goal'] != row['goal']
                counts['fine_model'] += 1
                counts['fine_partial'] += row['partial']
            else:
                counts['fine_bypass'] += 1
                row['partial'] = None
                assert words == row['goal'] and result == 1 and (dx, dy) == (0, 0)
            # Route order: exact source last, first is goal unless partial/nonmatching cell.
            assert words[-2:] == row['source'] or table['count'] == 1
            assert index == ((table['count'] - 2 if table['count'] > 1 else 0) if obstruction else 0)
            assert row['mismatch'] == (words[:2] != row['goal'])
            if engines:
                flags_bytes = bytes(w >> 24 for w in r.read(r.cells, SIDE * SIDE))
                start_cell = [math.floor(v) for v in source]
                goal_cell = [math.floor(v) for v in goal]
                q = (ctypes.c_uint32 * 16)(SIDE, SIDE, *start_cell, *goal_cell, 700, cls, 0x02000000, 0, 0, 0xffffffff,
                                           *row['source'], *row['goal'])
                wanted = [row['count'], index, int(row['mismatch'])] + words
                for e in engines:
                    e.pathing_fine_result_reset()
                    out = (ctypes.c_uint32 * 70000)()
                    e.pathing_fine_result_words(q, ctypes.byref(e.Objects((ctypes.c_uint8 * len(flags_bytes))(*flags_bytes), None)), out)
                    got = [out[3], out[4], out[6]] + list(out[7:7 + 2 * out[3]])
                    if captured:
                        got_extra = [out[0], out[1], out[2], out[5]]
                        want_extra = [int(not row['partial']), r.read(r.fine_system + 0x6c)[0], r.read(r.fine_system + 0x40)[0], obstruction]
                    else:
                        got_extra = want_extra = []
                    if got != wanted or got_extra != want_extra:
                        engine_cases['fine_differences'].append(dict(map=name, cls=cls, vector=[dx, dy], retail=wanted[:3] + want_extra,
                                                                     engine=got[:3] + got_extra, words_equal=got[3:] == wanted[3:]))
                    engine_cases['fine'] += 1
            fine_rows.append(row)
            # ---- coarse request, four lanes ----
            for lane in range(4):
                r.construct_path()
                r.set_buckets()
                r.uc.mem_write(r.path + 0xb4, struct.pack('<f', RADII[cls]))
                r.write(r.path + 0x88, (lane << 30) | 0x200000)
                acc_source = (f32(source[0] * .5), f32(source[1] * .5))
                acc_goal = (f32(goal[0] * .5), f32(goal[1] * .5))
                r.uc.mem_write(r.points + 0x50, struct.pack('<4f', *acc_source, *acc_goal))
                captured.clear()
                result, popped = r.run(0x6f166c30, r.path, r.points + 0x50, r.points + 0x58, 0)
                assert popped == 12, popped
                words = r.words(0x54)
                table = r.table(0x54)
                flags = r.read(r.path + 0x88)[0]
                arow = dict(map=name, cls=cls, lane=lane, vector=[dx, dy], goal_frac=[fw(v) for v in frac],
                            source=[fw(v) for v in acc_source], goal=[fw(v) for v in acc_goal], result=result,
                            count=table['count'], index=r.read(r.path + 0x78)[0], flags=flags,
                            adjusted=r.read(r.path + 0x24, 2), stored_size=r.read(r.system + 0x90)[0],
                            work=r.read(r.system + 0x9c)[0], words=words,
                            source_node=r.read(r.system + 0xc4)[0], goal_node=r.read(r.system + 0xc8)[0],
                            same_cell=[math.floor(v) for v in acc_source] == [math.floor(v) for v in acc_goal])
                counts['sizes_seen'].add((cls, arow['stored_size']))
                assert arow['stored_size'] == (1 if cls < 2 else 2)
                if captured:
                    c = captured[0]
                    assert len(captured) == 1 and c['kind'] == 'acc' and c['size'] == arow['stored_size']
                    expected = model_acc(c['chain'], c['size'], [wf(v) for v in c['source']], [wf(v) for v in c['goal']])
                    assert expected == words, (name, cls, lane, dx, dy, frac)
                    arow['chain'] = [list(n) for n in c['chain']]
                    arow['reconstruct_goal'] = c['goal']
                    arow['partial'] = c['goal'] != arow['goal']
                    for x, y, level, tag in c['chain']:
                        counts['acc_levels_seen'].add(level)
                        if arow['stored_size'] == 2 and level:
                            edge = lambda v: ((v >> level) << level) + (1 << level) - 1
                            counts['acc_level_decrements'] += (x == edge(x)) + (y == edge(y))
                    counts['acc_model'] += 1
                    counts['acc_partial'] += arow['partial']
                else:
                    counts['acc_bypass'] += 1
                    arow['partial'] = None
                    # Setup bypass (164c30 returned 1): one exact-goal point, index0, mismatch clear.
                    assert words == arow['goal'] and arow['index'] == 0 and result == 1
                    kind = 'same_cell' if arow['same_cell'] else 'source_node_-1' if arow['source_node'] == 0xffffffff else 'goal_node_equal'
                    counts.setdefault('acc_bypass_kinds', {}).setdefault(kind, 0)
                    counts['acc_bypass_kinds'][kind] += 1
                # Coarse index = count-1; mismatch flag/adjusted = first point * scale 2.
                assert arow['index'] == table['count'] - 1
                mismatch = words[:2] != arow['goal']
                assert bool(flags & 0x20000000) == mismatch
                if mismatch:
                    assert arow['adjusted'] == [fw(f32(wf(words[0]) * 2)), fw(f32(wf(words[1]) * 2))]
                assert words[-2:] == arow['source'] or table['count'] == 1
                if engines:
                    classes = [(r.read(storage + 8 * i + 4)[0] >> (30 - 2 * lane)) & 3
                               for storage, side in zip(r.data, r.sides) for i in range(side * side)]
                    q = (ctypes.c_uint32 * 8)(r.sides[0], r.sides[0], cls >> 1, 400, *arow['source'], *arow['goal'])
                    wanted = [int(not arow['partial']) if arow['partial'] is not None else 1, arow['work'],
                              r.read(r.system + 0x6c)[0], table['count']] + words
                    for e in engines:
                        out = (ctypes.c_uint32 * 40000)()
                        e.pathing_adaptive_route(q, (ctypes.c_uint8 * len(classes))(*classes), out)
                        got = list(out[:4 + 2 * out[3]])
                        if got != wanted:
                            engine_cases['coarse_differences'].append(dict(map=name, cls=cls, lane=lane, vector=[dx, dy],
                                retail=wanted[:4], engine=got[:4], words_equal=got[4:] == wanted[4:]))
                        engine_cases['coarse'] += 1
                acc_rows.append(arow)
    counts['acc_levels_seen'] = sorted(counts['acc_levels_seen'])
    counts['sizes_seen'] = sorted(counts['sizes_seen'])
    payload = dict(version=1, binary_sha256=r.digest, task='ROUTE-01.1',
                   scope=__doc__.strip().splitlines()[0],
                   fine=fine_rows, coarse=acc_rows)
    blob = json.dumps(payload, separators=(',', ':'), sort_keys=True).encode()
    digest = hashlib.sha256(blob).hexdigest()
    if args.expected:
        if args.expected.exists():
            frozen = gzip.decompress(args.expected.read_bytes()) if args.expected.suffix == '.gz' else args.expected.read_bytes()
            assert json.loads(frozen) == json.loads(blob), 'frozen ROUTE-01.1 words differ'
        else:
            args.expected.write_bytes(blob + b'\n')
    fixture_rows = 0
    if args.engine_fixture:
        fixture, fixture_rows = engine_fixture(payload)
        assert args.engine_fixture.read_text() == fixture, 'Move adapter fixture differs from original words'
    saved_functions = check_saved_evidence(args.ghidra_evidence, r.digest) if args.ghidra_evidence else 0
    differences = len(engine_cases['fine_differences']) + len(engine_cases['coarse_differences'])
    report = dict(passed=differences == 0, expected_equal=bool(args.expected and args.expected.exists()),
                  adapter_fixture_rows=fixture_rows, saved_functions=saved_functions,
                  engine_fine_cases=engine_cases['fine'], engine_coarse_cases=engine_cases['coarse'],
                  engine_differences=differences, binary_sha256=r.digest, payload_sha256=digest, fine_requests=len(fine_rows),
                  coarse_requests=len(acc_rows), **counts,
                  storm_calls=len(r.storm_log),
                  engine=dict(fine_cases=engine_cases['fine'], coarse_cases=engine_cases['coarse'],
                              fine_differences=len(engine_cases['fine_differences']),
                              coarse_differences=len(engine_cases['coarse_differences']),
                              fine_difference_rows=engine_cases['fine_differences'][:50],
                              coarse_difference_rows=engine_cases['coarse_differences'][:50],
                              libraries={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.engine_library}))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if differences:
        raise SystemExit('production reconstruction differs from original')


if __name__ == '__main__':
    main()
