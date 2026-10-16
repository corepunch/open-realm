"""Reject incomplete or altered shared-speed, clock, and control evidence."""
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_shared315 as v


class Shared315Evidence(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(v.SPEC.read_text())
        self.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def reject(self, edit):
        edit()
        with self.assertRaises(ValueError):
            v.validate(self.spec, self.bundle)

    def event(self, name, label=None):
        rows = (self.bundle['supplemental'][label]['rows'] if label else
                self.bundle['cases']['2'][0]['rows'])
        return next(row for row in rows if row['event'] == name)

    def test_complete(self):
        self.assertEqual(v.validate(self.spec, self.bundle), dict(
            captures=15, native_events=94809, public_markers=5520,
            fresh_motion_rows=3448, fresh_public_rows=1080, fresh_shared_rows=905, periodic_rows=16))

    def test_missing_scene(self):
        self.reject(lambda: self.bundle['cases'].pop('7'))

    def test_missing_repeat(self):
        self.reject(lambda: self.bundle['cases']['2'].pop(1))

    def test_missing_control(self):
        self.reject(lambda: self.bundle['cases']['2'].pop())

    def test_hooked_control(self):
        self.reject(lambda: self.bundle['cases']['2'][2]['rows'].insert(1, dict(event='trace-end')))

    def test_lost_native_event(self):
        self.reject(lambda: self.bundle['cases']['2'][0]['rows'].remove(self.event('commit-leave')))

    def test_source_hash(self):
        self.reject(lambda: self.bundle['cases']['2'][0]['rows'][0]['source_sha256']
                    .update({'shared315_observer.js': '0' * 64}))

    def test_owned_environment(self):
        self.reject(lambda: self.bundle['cases']['2'][0]['rows'][0].update(display=':97'))

    def test_control_public_differs(self):
        self.reject(lambda: self.bundle['cases']['2'][2].update(preload=''))

    def test_public_source_bits(self):
        self.reject(lambda: self.event('public-default-height').update(value=0))

    def test_setter_bits(self):
        self.reject(lambda: self.event('setter-enter').update(value=0))

    def test_public_getter(self):
        self.reject(lambda: self.event('public-coordinate').update(value=0x7fc00001))

    def test_physical_commit(self):
        def edit():
            rows = self.bundle['cases']['2'][0]['rows']
            created = next(i for i, row in enumerate(rows)
                           if row['event'] == 'marker' and 'label=created' in row['value'])
            identities = []
            for row in rows[:created]:
                if row['event'] == 'physical-radius' and row['m']['id'] not in identities:
                    identities.append(row['m']['id'])
            member = next(member for row in rows if row['event'] == 'commit-leave'
                          for member in row['g']['members'] if member['id'] in identities[-3:])
            member['m']['pos'][0] = 0
        self.reject(edit)

    def test_shared_accumulator(self):
        def edit():
            row = next(row for row in self.bundle['cases']['2'][0]['rows']
                       if row['event'] == 'shared-leave' and row['s'])
            row['s']['published'] = 0
        self.reject(edit)

    def test_shared_visit_order(self):
        def edit():
            rows = [r for r in self.bundle['cases']['2'][0]['rows']
                    if r['event'] == 'shared-leave' and r['c'] == 1310 and r['s']]
            self.assertEqual(len(rows), 2)
            rows[0]['s'], rows[1]['s'] = rows[1]['s'], rows[0]['s']
        self.reject(edit)

    def test_engine_words_cannot_replace_retail(self):
        self.reject(lambda: self.spec['cases']['2']['states']['motion'][0].__setitem__(3, 0))

    def test_missing_clock_repeat(self):
        self.reject(lambda: self.bundle['supplemental'].pop('init-observe-2'))

    def test_event_clock_domain(self):
        self.reject(lambda: self.event('captain-periodic-enter', 'timer-observe-2')['clocks'].update(timers=[]))

    def test_primary_clock(self):
        self.reject(lambda: self.event('captain-periodic-enter', 'init-observe-1')['clocks']['primary']
                    .__setitem__(0, 0))

    def test_range_callback_domain(self):
        self.reject(lambda: self.event('captain-range-enter-enter', 'init-observe-2')['clocks']
                    .update(timers=[dict(time=0)]))

    def test_captain_command(self):
        self.reject(lambda: self.event('captain-member-enter', 'init-observe-1').update(command=851986))

    def test_initial_command(self):
        self.reject(lambda: self.event('captain-init-leave', 'init-observe-2').update(order=0))

    def test_asynchronous_startup(self):
        self.reject(lambda: self.event('ai-thread-init-enter', 'init-observe-1').update(event='missing-startup'))

    def test_missing_instruction(self):
        self.reject(lambda: self.spec['instructions'].pop(next(iter(self.spec['instructions']))))

    def test_lowered_instruction_count(self):
        def edit():
            self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
            self.spec['instruction_count'] -= 1
        self.reject(edit)


if __name__ == '__main__':
    unittest.main()
