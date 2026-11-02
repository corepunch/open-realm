#!/usr/bin/env python3
"""Execute original fine/accelerated route reconstruction over synthetic chains.

No function stubs. Preallocated route storage and four observed initialized
runtime constants are supplied explicitly; search/placement are separate.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import struct
import subprocess
import sys
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--all-directions', action='store_true', help='extend fine chains to every cardinal/diagonal direction')
    parser.add_argument('--fixture', type=Path, help='freeze exact original fine coordinate words')
    parser.add_argument('--engine-library', type=Path, help='compare production fine reconstruction')
    parser.add_argument('--outside-axis-fixture', type=Path, help='completing public outside-axis controls')
    parser.add_argument('--buffer-fixture', type=Path, help='frozen public buffer/invalid-start controls')
    parser.add_argument('--buffer-ghidra-evidence', type=Path, help='saved buffer/source Ghidra readback')
    parser.add_argument('--consumer-fixture', type=Path, help='public controls with original coarse-distance initialization')
    parser.add_argument('--invalid-consumer-fixture', type=Path, help='complete initialized invalid consumers and owner RNG')
    parser.add_argument('--invalid-ghidra-evidence', type=Path)
    parser.add_argument('--search-load-fixture', type=Path, help='repeated UI-load search resets with an observer-free control')
    parser.add_argument('--consumer-ghidra-evidence', type=Path, help='saved consumer/initializer annotations')
    parser.add_argument('--producer-fixture', type=Path, help='also run the complete ROUTE-01.1 producer corpus')
    parser.add_argument('--engine-fixture', type=Path, help='literal Move adapter regression subset')
    parser.add_argument('--ghidra-evidence', type=Path, help='saved reconstruction function annotations')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_fine_reconstruct.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint32)]
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])

    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    system, nodes, route, data = 0x10000000, 0x10001000, 0x10000200, 0x10002000
    start_ptr, goal_ptr, stack, stop = 0x10000300, 0x10000310, 0x20008000, 0x30000000
    sentinel = -128000.0078125
    constants = {'6fd3c740': -1.0, '6fd3c744': 0.0, '6fd3c748': 1.0, '6fd53a74': sentinel}
    for address, value in constants.items():
        machine.mem_write(int(address, 16), struct.pack('<f', value))

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def reconstruct(entry, points, start, goal, size=1, level=0, tag=0):
        machine.mem_write(nodes, bytes(36 * len(points)))
        for index, (x, y) in enumerate(points):
            write(nodes + index * 36, x, y)
            write(nodes + index * 36 + 0x1c, index - 1)
            if index == 1:
                machine.mem_write(nodes + index * 36 + 0x22, bytes((level, tag)))
        write(system + 0x30, nodes)
        write(system + 0x5c, nodes)
        write(system + 0x90, size)
        write(system + 0xa0, 0)
        write(route + 0xc, data)
        write(route + 0x18, 64, 0)
        machine.mem_write(start_ptr, struct.pack('<ff', *start))
        machine.mem_write(goal_ptr, struct.pack('<ff', *goal))
        write(stack, stop, len(points) - 1, route, start_ptr, goal_ptr)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, system)
        machine.emu_start(entry, stop, count=100000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail reconstruction exceeded instruction budget')
        count = struct.unpack('<I', machine.mem_read(route + 0x1c, 4))[0]
        if count > 64:
            raise RuntimeError('retail reconstruction exceeded preallocated capacity')
        return [struct.unpack('<ff', machine.mem_read(data + i * 8, 8)) for i in range(count)]

    fine_cases = 0
    frozen = []
    directions = ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)) if args.all_directions else ((1,1),)
    for length, ox, oy, delta, direction in itertools.product(range(1, 6), (-2, 0, 10, 1000), (-2, 0, 10, 1000),
                                                  ((0, 0), (.125, .875), (.999, .001), (1, 0), (0, 1), (-.01, 0)), directions):
        points = [(ox + direction[0] * i, oy + direction[1] * i) for i in range(length)]
        start = (ox + .25, oy + .75)
        # Round reference input to the same float32 values supplied to x86.
        goal = struct.unpack('<ff', struct.pack('<ff', points[-1][0] + delta[0], points[-1][1] + delta[1]))
        expected = [(x + .5, y + .5) for x, y in reversed(points)]
        expected[-1] = start
        if tuple(map(math.floor, expected[0])) == tuple(map(math.floor, goal)):
            expected[0] = goal
        actual = reconstruct(0x6f147dc0, points, start, goal)
        if actual != expected:
            raise RuntimeError(f'fine reconstruction mismatch points={points} goal={goal}: {actual} != {expected}')
        input_words = list(struct.unpack('<IIII', struct.pack('<ffff', *start, *goal)))
        actual_words = list(struct.unpack('<' + 'I' * (2 * len(actual)), b''.join(struct.pack('<ff', *v) for v in actual)))
        if engine:
            inp = (ctypes.c_uint32 * 5)(length, *input_words)
            cells = (ctypes.c_int32 * (2 * length))(*(v for point in points for v in point))
            for repeat in range(2):
                output = (ctypes.c_uint32 * 129)()
                engine.pathing_fine_reconstruct(inp, cells, output)
                if output[0] != len(actual) or list(output[1:1+2*output[0]]) != actual_words:
                    raise RuntimeError('production fine reconstruction words differ')
        if args.fixture:
            frozen.append(dict(cells=points, input=input_words, output=actual_words))
        fine_cases += 1

    accelerated_cases = 0
    for size, level, x, y, tag in itertools.product((1, 2, 4), range(4), range(16), range(16), (0, 1, 255)):
        points, start, goal = [(0, 0), (x, y), (20, 20)], (.125, .375), (20.875, 20.125)
        ax, ay = x, y
        if size == 2 and level:
            if x == ((x >> level) << level) + (1 << level) - 1:
                ax -= 1
            if y == ((y >> level) << level) + (1 << level) - 1:
                ay -= 1
        offset = 1.25 if size == 2 else .75
        expected = [goal, (ax + offset, ay + offset)]
        if tag:
            expected.append((sentinel, float(tag)))
        expected.append(start)
        actual = reconstruct(0x6f162a30, points, start, goal, size, level, tag)
        marker_count = struct.unpack('<I', machine.mem_read(system + 0xa0, 4))[0]
        if actual != expected or marker_count != bool(tag):
            raise RuntimeError(f'acc reconstruction mismatch size={size} level={level} xy={(x,y)} tag={tag}: {actual} != {expected}')
        accelerated_cases += 1
    report = dict(binary_sha256=digest, fine_cases=fine_cases, accelerated_cases=accelerated_cases,
                  mismatches=[], runtime_constants=constants,
                  scope='original reconstruction/float helpers/container append; synthetic valid parent chains; preallocated storage; tag only on middle node; no smoothing or destination placement')
    if engine:
        report.update(engine_queries=fine_cases, engine_repeats=fine_cases, engine_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    if args.fixture:
        args.fixture.write_text(json.dumps(dict(binary_sha256=digest, scope='Original147dc0 fine coordinate words; preallocated valid parent chains, eight directions and lengths1..5. Coarse policy and buffer growth separate.', cases=frozen), separators=(',', ':')) + '\n')
    if args.producer_fixture:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        producer_report = args.report.with_name(args.report.stem + '-producers.json')
        command = [sys.executable, str(Path(__file__).with_name('research') / 'verify_route01_1_reconstruction.py'),
                   '--binary', str(args.binary), '--report', str(producer_report), '--expected', str(args.producer_fixture)]
        for option, path in (('--engine-library', args.engine_library), ('--engine-fixture', args.engine_fixture),
                             ('--ghidra-evidence', args.ghidra_evidence)):
            if path:
                command += [option, str(path)]
        subprocess.run(command, check=True)
        producer = json.loads(producer_report.read_text())
        for key in ('fine_requests', 'coarse_requests', 'engine_fine_cases', 'engine_coarse_cases', 'engine_differences',
                    'expected_equal', 'adapter_fixture_rows', 'saved_functions', 'payload_sha256'):
            report['producer_' + key] = producer[key]
    if args.buffer_fixture:
        buffer_report = args.report.with_name(args.report.stem + '-buffers.json')
        command = [sys.executable, str(Path(__file__).parent / 'research' / 'verify_route01_2_buffers.py'),
                   '--binary', str(args.binary), '--report', str(buffer_report), '--expected', str(args.buffer_fixture)]
        if args.engine_library:
            command += ['--engine', str(args.engine_library)]
        subprocess.run(command, check=True)
        buffers = json.loads(buffer_report.read_text())
        report['buffer_scenarios'] = buffers['rows']
        report['buffer_growth_cases'] = buffers['growth_cases']
        report['buffer_payload_sha256'] = buffers['payload_sha256']
        if args.engine_library:
            report['buffer_engine_fine_builds'] = buffers['engine_fine_builds']
            report['buffer_engine_coarse_builds'] = buffers['engine_coarse_builds']
            report['buffer_engine_differences'] = len(buffers['engine_differences'])
        if args.buffer_ghidra_evidence:
            saved = json.loads(args.buffer_ghidra_evidence.read_text())
            assert saved['binary_sha256'] == digest and not saved['unsaved_changes']
            assert len(saved['functions']) == 12
            assert all('Payoff141 ROUTE-01.2:' in f['comment'] for f in saved['functions'])
            report['buffer_saved_functions'] = len(saved['functions'])
    if args.consumer_fixture:
        consumer_report = args.report.with_name(args.report.stem + '-consumers.json')
        command = [sys.executable, str(Path(__file__).parent / 'research' / 'verify_route01_2_buffers.py'),
                   '--binary', str(args.binary), '--report', str(consumer_report),
                   '--expected', str(args.consumer_fixture), '--initialized-consumer']
        if args.engine_library:
            command += ['--engine', str(args.engine_library)]
        subprocess.run(command, check=True)
        consumers = json.loads(consumer_report.read_text())
        for key in ('rows', 'growth_cases', 'payload_sha256'):
            report['consumer_' + key] = consumers[key]
        if args.engine_library:
            report['consumer_engine_fine_builds'] = consumers['engine_fine_builds']
            report['consumer_engine_coarse_builds'] = consumers['engine_coarse_builds']
            report['consumer_engine_differences'] = len(consumers['engine_differences'])
        subprocess.run([sys.executable, str(Path(__file__).parent / 'research' / 'export_route012_consumers.py'), '--check'], check=True)
        report['consumer_adapter_rows'] = 208
        if args.consumer_ghidra_evidence:
            saved = json.loads(args.consumer_ghidra_evidence.read_text())
            assert saved['binary_sha256'] == digest and not saved['unsaved_changes']
            assert len(saved['functions']) == 4
            assert all('Payoff142 ROUTE-01.2:' in f['comment'] for f in saved['functions'])
            report['consumer_saved_functions'] = len(saved['functions'])
    if args.invalid_consumer_fixture:
        invalid_report = args.report.with_name(args.report.stem + '-invalid-consumers.json')
        subprocess.run([sys.executable, str(Path(__file__).parent / 'research' / 'verify_route012_invalid_consumers.py'),
                        '--binary', str(args.binary), '--report', str(invalid_report),
                        '--expected', str(args.invalid_consumer_fixture)], check=True)
        invalid = json.loads(invalid_report.read_text())
        report['invalid_consumer_rows'] = len(invalid['rows'])
        report['invalid_consumer_advances'] = sum(2 for row in invalid['rows'])
        report['invalid_consumer_expected_equal'] = True
        subprocess.run([sys.executable, str(Path(__file__).parent / 'research' / 'export_route012_invalid_consumers.py'), '--check'], check=True)
        if args.invalid_ghidra_evidence:
            saved = json.loads(args.invalid_ghidra_evidence.read_text())
            assert saved['binary_sha256'] == digest and not saved['unsaved_changes']
            assert len(saved['functions']) == 7
            assert all('Payoff143' in f['comment'] for f in saved['functions'])
            assert {f['address'] for f in saved['functions']} == {
                '6f147600', '6f14f570', '6f1481e0', '6f148210', '6f162da0', '6f162fd0', '6f165c60'}
            report['invalid_saved_functions'] = len(saved['functions'])
    if args.search_load_fixture:
        load_report = args.report.with_name(args.report.stem + '-search-load.json')
        command = [sys.executable, str(Path(__file__).parents[1] / 'frida' / 'research' / 'route012_search_load_summarize.py'),
                   '--report', str(load_report), '--expected', str(args.search_load_fixture), '--captures']
        command += [str(args.search_load_fixture.parent / ('retail-route-search-load-' + variant + '-1.27.jsonl.gz'))
                    for variant in ('first', 'repeat', 'control')]
        command += ['--preloads'] + [str(args.search_load_fixture.parent / ('retail-route-search-load-' + variant + '-1.27-preload.txt.gz'))
                                     for variant in ('first', 'repeat', 'control')]
        subprocess.run(command, check=True)
        loaded = json.loads(load_report.read_text())
        for key in ('observer_repeats', 'observer_free_controls', 'full_public_sequences_equal'):
            report['search_load_' + key] = loaded[key]
    if args.outside_axis_fixture:
        axis_report = args.report.with_name(args.report.stem + '-outside-axis.json')
        command = [sys.executable, str(Path(__file__).parents[1] / 'frida' / 'research' / 'route012_summarize.py'),
                   '--report', str(axis_report), '--expected', str(args.outside_axis_fixture)]
        for variant in ('first', 'repeat', 'control'):
            command += ['--' + variant, str(args.outside_axis_fixture.parent / ('retail-outside-axis-' + variant + '-1.27.jsonl.gz'))]
        subprocess.run(command, check=True)
        axis = json.loads(axis_report.read_text())
        for key in ('public_outside_sources', 'completing_runs', 'observer_free_controls'):
            report['buffer_axis_' + key] = axis[key]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{fine_cases} fine / {accelerated_cases} accelerated reconstruction cases; all pass')


if __name__ == '__main__':
    main()
