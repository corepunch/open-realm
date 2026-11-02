#!/usr/bin/env python3
"""Keep the retreat473 moving-roster retreat evidence, retail save/load, engine literals and pins strict."""
import copy, json, sys, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ghidra'))
import verify_wc3_pathing_captain473 as v  # noqa: E402

ERRORS = (ValueError, AssertionError, KeyError, StopIteration, IndexError, TypeError)


class Captain473Retreat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.SPEC.read_text())
        cls.bundle, cls.result = v.validate(cls.spec)
        cls.headers = {k: p.read_text() for k, p in v.HEADERS.items()}

    def reject(self, change, headers=None):
        bundle = copy.deepcopy(self.bundle)
        change(bundle)
        with self.assertRaises(ERRORS):
            v.validate_runtime(bundle, headers or self.headers, self.spec['counts'])

    @staticmethod
    def family(bundle, name):
        return next(f for f in bundle['families'] if f['name'] == name)

    @staticmethod
    def edit(capture, predicate, change):
        rows = [json.loads(x) for x in capture['raw_jsonl'].splitlines()]
        change(next(r for r in rows if predicate(r)))
        capture['raw_jsonl'] = '\n'.join(json.dumps(x) for x in rows) + '\n'

    def edit_observations(self, bundle, name, predicate, change):
        for cap in self.family(bundle, name)['captures'][:-1]:
            self.edit(cap, predicate, change)

    def test_accepted_counts(self):
        self.assertEqual(self.result, self.spec['counts'])
        self.assertEqual(self.result['retreat473']['public_rows'], 1746)
        self.assertEqual(self.result['retreat473b']['captain_snapshots'], 289)
        self.assertGreaterEqual(self.result['retreat473']['observations'], 2)
        self.assertTrue(any(o['saved_flags'] == 0x1003 for o in self.result['saveload']['observations']))

    def test_header_must_be_regenerated_exactly(self):
        headers = dict(self.headers)
        headers['retreat473'] = headers['retreat473'].replace('0x44986ddcu', '0x44986dddu', 1)
        self.reject(lambda b: None, headers)

    def test_second_family_header_is_literal(self):
        headers = dict(self.headers)
        headers['retreat473b'] = headers['retreat473b'].replace('static uint32_t const retreat473b_captain', 'static uint32_t const retreat473_captain', 1)
        self.reject(lambda b: None, headers)

    def test_public_getter_word_is_literal(self):
        self.reject(lambda b: self.edit(self.family(b, 'retreat473')['captures'][0],
                                        lambda r: r.get('event') == 'public-position' and r['tick'] == 200,
                                        lambda r: r['xy'].__setitem__(0, r['xy'][0] ^ 1)))

    def test_repeat_difference_rejected(self):
        self.reject(lambda b: self.edit(self.family(b, 'retreat473')['captures'][1],
                                        lambda r: r.get('event') == 'captain-call' and r['name'] == 'goal-event',
                                        lambda r: r.update(c=r['c'] + 1)))

    def test_retreat_reissue_caller_is_literal(self):
        self.reject(lambda b: self.edit_observations(b, 'retreat473', lambda r: r.get('event') == 'reissue' and r['tick'] == 135,
                                                     lambda r: r.update(caller=0x9d7b9f)))

    def test_retreat_reissue_point_is_authored_home(self):
        self.reject(lambda b: self.edit_observations(b, 'retreat473', lambda r: r.get('event') == 'reissue' and r['tick'] == 135,
                                                     lambda r: r.update(target='0x1234')))

    def test_second_removal_reevaluation_required(self):
        self.reject(lambda b: self.edit_observations(b, 'retreat473b',
                                                     lambda r: r.get('event') == 'captain-call' and r['name'] == 'home-reevaluate' and r['tick'] == 139,
                                                     lambda r: r.update(result=0)))

    def test_control_must_be_unhooked(self):
        def change(b):
            cap = self.family(b, 'retreat473')['captures'][-1]
            rows = [json.loads(x) for x in cap['raw_jsonl'].splitlines()]
            rows.insert(1, {'event': 'module', 'base': '0x0'})
            cap['raw_jsonl'] = '\n'.join(json.dumps(x) for x in rows) + '\n'
        self.reject(change)

    def test_control_required(self):
        self.reject(lambda b: self.family(b, 'retreat473b')['captures'].pop())

    def test_control_public_marker_stream(self):
        def change(b):
            cap = self.family(b, 'retreat473')['captures'][-1]
            cap['preload'] = cap['preload'].replace('label=remove', 'label=removed', 1)
        self.reject(change)

    def test_record_order_is_literal(self):
        def change(b):
            for cap in self.family(b, 'retreat473')['captures'][:-1]:
                rows = [json.loads(x) for x in cap['raw_jsonl'].splitlines()]
                j = next(k for k, r in enumerate(rows) if r.get('event') == 'roster' and r['tick'] == 135)
                rows[j], rows[j + 1] = rows[j + 1], rows[j]
                cap['raw_jsonl'] = '\n'.join(json.dumps(x) for x in rows) + '\n'
        self.reject(change)

    def test_saveload_post_load_getter_is_literal(self):
        def change(b):
            cap = next(c for c in b['saveload']['captures'] if c['name'] == 'sl-explore-2')
            rows = [json.loads(x) for x in cap['raw_jsonl'].splitlines()]
            last = max(i for i, r in enumerate(rows) if r.get('event') == 'public-position')
            rows[last]['xy'][1] ^= 1
            cap['raw_jsonl'] = '\n'.join(json.dumps(x) for x in rows) + '\n'
        self.reject(change)

    def test_saveload_retreat_window_observation_required(self):
        self.reject(lambda b: b['saveload'].update(captures=[c for c in b['saveload']['captures'] if c['name'] != 'sl-explore-2']))

    def test_saveload_retreat_window_control_required(self):
        self.reject(lambda b: b['saveload'].update(captures=[c for c in b['saveload']['captures'] if c['name'] != 'sl-control-2']))

    def test_saveload_control_suffix_is_literal(self):
        def change(b):
            cap = next(c for c in b['saveload']['captures'] if c['name'] == 'sl-control-2')
            i = cap['preload'].rindex('label=unit u=5')
            cap['preload'] = cap['preload'][:i] + cap['preload'][i:].replace('order=0', 'order=1', 1)
        self.reject(change)

    def test_source_pins_enforced(self):
        spec = copy.deepcopy(self.spec)
        spec['pins'][next(iter(spec['pins']))] = '0' * 64
        with self.assertRaises(ValueError):
            v.validate(spec)

    def test_guard_inventory_complete(self):
        spec = copy.deepcopy(self.spec)
        spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):
            v.validate(spec)


if __name__ == '__main__':
    unittest.main()
