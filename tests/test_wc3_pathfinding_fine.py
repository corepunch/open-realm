"""Production WC3 search against frozen original-x86 static searches."""
import ctypes
import hashlib
import json
import itertools
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_grid import footprint_graph, ObjectInput
from verify_wc3_pathing_numeric import bits


class FineSearchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-fine-grid-1.27.json').read_text())
        cls.temp = tempfile.TemporaryDirectory(prefix='wc3-fine-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.engines = []
        for opt in ('-O0', '-O2'):
            lib = Path(cls.temp.name) / (opt + '.so')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', opt, '-fPIC', '-shared',
                            '-I', str(ROOT), str(ROOT / 'tools/ghidra/wc3_pathing_engine_probe.c'), '-o', str(lib)], check=True)
            engine = ctypes.CDLL(str(lib))
            engine.pathing_fine_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8),
                                                ctypes.POINTER(ctypes.c_int32)]
            cls.engines.append(engine)

    def test_stock_movement_profiles_and_wpm_masks_match_original(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-wpm-movement-masks-1.27.json').read_text())
        for engine in self.engines:
            engine.pathing_wpm_flags.argtypes = [ctypes.c_uint32]
            engine.pathing_wpm_flags.restype = ctypes.c_uint32
            for raw, original in enumerate(fixture['wpm_masks']):
                self.assertEqual(engine.pathing_wpm_flags(raw) & 0xc6, (original >> 24) & 0xc6, raw)

    def test_fine_reconstruction_words_match_original_in_every_direction(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-fine-reconstruction-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 3840)
        for engine in self.engines:
            engine.pathing_fine_reconstruct.argtypes = [ctypes.POINTER(ctypes.c_uint32),
                                                       ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint32)]
            for case in fixture['cases']:
                inp = (ctypes.c_uint32 * 5)(len(case['cells']), *case['input'])
                cells = (ctypes.c_int32 * (2 * len(case['cells'])))(*(v for p in case['cells'] for v in p))
                for repeat in range(2):
                    output = (ctypes.c_uint32 * 129)()
                    engine.pathing_fine_reconstruct(inp, cells, output)
                    with self.subTest(input=case['input'], cells=case['cells'], repeat=repeat, opt=engine._name):
                        self.assertEqual(output[0] * 2, len(case['output']))
                        self.assertEqual(list(output[1:1+2*output[0]]), case['output'])

    def graph(self, case):
        # The original footprint oracle owns this graph, independently of the
        # search policy. Production's initial world adapter keeps its own legality.
        width, height = self.fixture['dimensions']
        cells = bytes.fromhex(self.fixture['maps'][case['fixture']])
        blocked = {(x, y) for y in range(height) for x in range(width) if cells[y * width + x]}
        return footprint_graph(blocked, (width, height), case['size_class'])

    def run_search(self, engine, graph, budget=2048):
        inp = (ctypes.c_uint32 * 7)(*self.fixture['dimensions'], *self.fixture['start'], *self.fixture['goal'], budget)
        edges = (ctypes.c_uint8 * len(graph))(*graph)
        out = (ctypes.c_int32 * (6 + 2 * 16386))()
        engine.pathing_fine_grid(inp, edges, out)
        return list(out[:6]), [[out[6 + 2 * i], out[7 + 2 * i]] for i in range(out[3])]

    def test_complete_routes_costs_work_and_node_creation_match_original(self):
        self.assertEqual(len(self.fixture['cases']), 288)
        graphs = [self.graph(case) for case in self.fixture['cases']]
        for engine in self.engines:
            stale = 0
            for case, graph in zip(self.fixture['cases'], graphs):
                with self.subTest(case=case['fixture'], size=case['size_class'], engine=engine._name):
                    head, path = self.run_search(engine, graph)
                    self.assertEqual(head[:3], [case['cost'] if case['cost'] is not None else -1,
                                               case['pops'], case['nodes']])
                    self.assertEqual(path, case['path'])
                    self.assertEqual(self.run_search(engine, graph), (head, path))
                    stale += head[5]
            self.assertGreater(stale, 0)

    def test_complete_object_routes_and_stamp_reuse_match_original(self):
        for name in ('retail-fine-objects-1.27.json', 'retail-fine-movement-profiles-1.27.json'):
            fixture = json.loads((ROOT / 'tools/ghidra/fixtures' / name).read_text())
            for engine in self.engines:
                engine.pathing_fine_objects.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput),
                                                       ctypes.POINTER(ctypes.c_int32)]
                for case in fixture['cases']:
                    profile = fixture['profiles'][case['fixture']]
                    objects, mask = profile['objects'], profile['mask']
                    width, height = fixture['dimensions']
                    cells = bytes.fromhex(fixture['maps'][case['fixture']])
                    terrain = (ctypes.c_uint8 * len(cells))(*(mask >> 24 if c else 0 for c in cells))
                    raw = [v for obj in objects for v in (*obj['bounds'], obj['mask'], obj['flags'], obj['linked'])]
                    object_words = (ctypes.c_uint32 * len(raw))(*raw)
                    data = ObjectInput(terrain, object_words)
                    inp = (ctypes.c_uint32 * 11)(width, height, *fixture['start'], *fixture['goal'],
                                                2048, case['size_class'], mask, 0, len(objects))
                    for repeat in range(2):
                        out = (ctypes.c_int32 * (6 + 2 * 16386))()
                        engine.pathing_fine_objects(inp, ctypes.byref(data), out)
                        with self.subTest(case=case['fixture'], cls=case['size_class'], repeat=repeat, opt=engine._name):
                            self.assertEqual(list(out[:4]), [case['cost'] if case['cost'] is not None else -1,
                                                            case['pops'], case['nodes'], len(case['path'])])
                            self.assertEqual([[out[6 + 2*i], out[7 + 2*i]] for i in range(out[3])], case['path'])

    def test_suppressed_target_identity_exits_match_original_searches(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-fine-targets-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 2304)
        for engine in self.engines:
            engine.pathing_fine_target.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput),
                                                   ctypes.POINTER(ctypes.c_int32)]
            for case in fixture['cases']:
                width, height = case['input'][:2]
                blocked = {tuple(p) for p in case['blocked']}
                terrain = (ctypes.c_uint8 * (width * height))(*(case['input'][8] >> 24 if (x,y) in blocked else 0
                    for y in range(height) for x in range(width)))
                raw = [v for obj in case['objects'] for v in obj]
                data = ObjectInput(terrain, (ctypes.c_uint32 * len(raw))(*raw))
                inp = (ctypes.c_uint32 * 12)(*case['input'])
                for repeat in range(2):
                    out = (ctypes.c_int32 * 4096)()
                    engine.pathing_fine_target(inp, ctypes.byref(data), out)
                    with self.subTest(bounds=case['bounds'], terrain=case['terrain'], variant=case['variant'],
                                      query=case['input'], repeat=repeat, opt=engine._name):
                        self.assertEqual(list(out[:7]), case['output'])
                        self.assertEqual([[out[7+2*i], out[8+2*i]] for i in range(out[3])], case['path'])

    def test_original_nearest_chains_survive_request_budget_boundaries(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-fine-partials-1.27.json').read_text())
        self.assertEqual(len(fixture['budget_cases']), 1456)
        reached = exhausted = zero_distance_failures = 0
        for engine in self.engines:
            engine.pathing_fine_partial.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput),
                                                   ctypes.POINTER(ctypes.c_int32)]
            for case in fixture['budget_cases']:
                profile = fixture['profiles'][case['fixture']]
                objects, mask = profile['objects'], profile['mask']
                cells = bytes.fromhex(fixture['maps'][case['fixture']])
                terrain = (ctypes.c_uint8 * len(cells))(*(mask >> 24 if c else 0 for c in cells))
                raw = [v for obj in objects for v in (*obj['bounds'], obj['mask'], obj['flags'], obj['linked'])]
                words = (ctypes.c_uint32 * len(raw))(*raw)
                data = ObjectInput(terrain, words)
                inp = (ctypes.c_uint32 * 11)(*fixture['dimensions'], *fixture['start'], *fixture['goal'],
                    case['budget'], case['size_class'], mask, 0, len(objects))
                for repeat in range(2):
                    out = (ctypes.c_int32 * (7 + 2 * 16386))()
                    engine.pathing_fine_partial(inp, ctypes.byref(data), out)
                    with self.subTest(case=case['fixture'], cls=case['size_class'], budget=case['budget'],
                                      repeat=repeat, opt=engine._name):
                        self.assertEqual(list(out[:7]), [case['result'], case['pops'], case['nodes'],
                                         len(case['path']), *case['nearest'], case['distance']])
                        self.assertEqual([[out[7+2*i], out[8+2*i]] for i in range(out[3])], case['path'])
                reached += case['result']
                exhausted += not case['result']
                zero_distance_failures += not case['result'] and case['distance'] == 0
        self.assertGreater(reached, 0)
        self.assertGreater(exhausted, 0)
        # The nearest node can already be the goal when its final pop is denied.
        self.assertGreater(zero_distance_failures, 0)

    def test_original_queue_witnesses_cover_reopening_and_equal_keys(self):
        # verify_wc3_pathing_queue.py executes original 14a560 for these values.
        # The complete 288-map corpus above does not exercise a closed reopen.
        for engine in self.engines:
            engine.pathing_fine_relax.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_fine_heap_ties.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            order = (ctypes.c_uint32 * 8)()
            engine.pathing_fine_heap_ties(order)
            self.assertEqual(list(order), [7, 0, 4, 1, 2, 3, 5, 6])
            for state in range(3):
                for cost in (120, 121, 122):
                    out = (ctypes.c_uint32 * 7)()
                    engine.pathing_fine_relax(state, cost, out)
                    accepted = state == 0 or cost > 121
                    self.assertEqual(list(out), [11 if state == 0 else 12, 121, 83, 3, 1,
                                                2 if state == 1 else 1, int(state == 2)] if accepted
                                     else [10, cost, 83, 9, state, int(state == 1), 0])

    def test_static_endpoints_match_complete_original_validator(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-footprint-endpoints-1.27.json').read_text())
        queries = list(itertools.product(fixture['radius_words'], fixture['position_words'], fixture['blockers']))
        self.assertEqual(len(queries), 1184)
        self.assertEqual(len(fixture['results']), len(queries))
        width, height = fixture['dimensions']
        for engine in self.engines:
            engine.pathing_footprint.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8)]
            engine.pathing_footprint.restype = ctypes.c_uint32
            for (radius, pos, blocker), expected in zip(queries, fixture['results']):
                cells = (ctypes.c_uint8 * (width * height))()
                if blocker is not None:
                    cells[blocker[1] * width + blocker[0]] = fixture['query']
                query = (ctypes.c_uint32 * 6)(radius, *pos, width, height, fixture['query'])
                self.assertEqual(engine.pathing_footprint(query, cells), expected)

    def test_wall_endpoints_use_retail_cells_instead_of_circle_clearance(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-footprint-endpoints-1.27.json').read_text())
        width, height = fixture['dimensions']
        self.assertEqual(len(fixture['wall_endpoints']), 4)
        for engine in self.engines:
            engine.pathing_footprint.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8)]
            engine.pathing_footprint.restype = ctypes.c_uint32
            for row in fixture['wall_endpoints']:
                cells = (ctypes.c_uint8 * (width * height))()
                for y in range(height): cells[y * width + 7] = fixture['query']
                x, y = row['world_position']
                query = (ctypes.c_uint32 * 6)(bits(row['radius_world'] / 32), bits(x / 32), bits(y / 32), width, height, fixture['query'])
                self.assertEqual(engine.pathing_footprint(query, cells), row['result'])

    def test_original_sampled_segments_match_results_and_cell_order(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-sampled-segments-1.27.json').read_text())
        self.assertEqual(len(fixture['configs']), 7168)
        for engine in self.engines:
            engine.pathing_segment.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8),
                                               ctypes.POINTER(ctypes.c_int32)]
            engine.pathing_segment_normalize.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
            for case in fixture['normalizers']:
                inp, out = (ctypes.c_uint32 * 2)(*case['input']), (ctypes.c_uint32 * 3)()
                engine.pathing_segment_normalize(inp, out)
                self.assertEqual(list(out), case['result'])
            digest = hashlib.sha256(); count = 0
            for config in fixture['configs']:
                inp = (ctypes.c_uint32 * 9)(*config['start'], *config['direction'], config['length'],
                                          config['class'], *fixture['dimensions'], config['mask'])
                for blocker, expected in zip(config['blockers'], config['results']):
                    cells = (ctypes.c_uint8 * 256)(); out = (ctypes.c_int32 * 1026)()
                    if blocker is not None: cells[blocker[1] * 16 + blocker[0]] = config['mask']
                    engine.pathing_segment(inp, cells, out)
                    self.assertEqual(out[0], expected, (engine._name, config, blocker))
                    self.assertLessEqual(out[1], 512)
                    digest.update(struct.pack('<2I', out[0], out[1]))
                    for i in range(out[1]): digest.update(struct.pack('<2i', out[2 + 2*i], out[3 + 2*i]))
                    count += 1
            self.assertEqual(count, 43244)
            self.assertEqual(digest.hexdigest(), fixture['visit_digest'])

    def test_original_waypoint_selection_and_commit_indices(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-sampled-segments-1.27.json').read_text())
        route_file = ROOT / 'tools/ghidra/fixtures' / fixture['route_fixture']
        self.assertEqual(hashlib.sha256(route_file.read_bytes()).hexdigest(), fixture['route_fixture_sha256'])
        routes = json.loads(route_file.read_text())
        self.assertEqual(len(fixture['route_cases']), 123)
        for engine in self.engines:
            engine.pathing_segment_waypoint.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8),
                                                        ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_segment_waypoint.restype = ctypes.c_uint32
            for result in fixture['route_cases']:
                case = routes['cases'][result['case']]
                words = [struct.unpack('<I', struct.pack('<f', v + .5))[0] for p in reversed(case['path']) for v in p]
                start = [struct.unpack('<I', struct.pack('<f', v + .5))[0] for v in routes['start']]
                raw = bytes.fromhex(routes['maps'][case['fixture']])
                inp = (ctypes.c_uint32 * 7)(*start, case['size_class'], len(case['path']) - 1, *routes['dimensions'], 2)
                cells = (ctypes.c_uint8 * len(raw))(*(2 if v else 0 for v in raw))
                points = (ctypes.c_uint32 * len(words))(*words)
                self.assertEqual(engine.pathing_segment_waypoint(inp, cells, points), result['selected'])

    def test_zero_budget_counts_rejected_iteration_and_reuse_recovers(self):
        case = self.fixture['cases'][0]
        graph = self.graph(case)
        for engine in self.engines:
            head, path = self.run_search(engine, graph, 0)
            self.assertEqual(head[:4], [-1, 1, 2, 0])
            self.assertEqual(path, [])
            head, path = self.run_search(engine, graph)
            self.assertEqual(head[:3], [case['cost'], case['pops'], case['nodes']])
            self.assertEqual(path, case['path'])


if __name__ == '__main__':
    unittest.main()
