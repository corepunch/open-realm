"""Public Stop evidence preserves live/cached radii and authenticates fine-target pointers."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_cancel_trace import canonical_lifecycle, verify_contract


class CaptainCancelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-cancel-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f)

    def test_complete_both_timings_and_engine_references(self):
        verify_contract(self.fixture)
        for v in self.fixture['variants']:
            header = ROOT / 'games/warcraft-3/game/tests' / f'retail_captain_cancel_{v["name"]}.h'
            self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), v['engine_header_sha256'])

    def test_before_shared_stop_cannot_be_omitted(self):
        self.rejects(lambda f: f['variants'].pop(0))

    def test_after_shared_stop_requires_full_motion(self):
        self.rejects(lambda f: f['variants'][1].__setitem__('engine_commits', 5458))

    def test_live_radius_must_change_without_cached_footprint(self):
        self.rejects(lambda f: f['variants'][1]['state']['footprint_transitions'][1].__setitem__(3, 0x3f780000))

    def test_surviving_group_retains_one_reference(self):
        self.rejects(lambda f: f['variants'][1]['state'].__setitem__('shared_reference_counts', [2]))

    def test_reused_shared_slot_requires_new_generation(self):
        self.rejects(lambda f: f['variants'][0]['state']['shared_identities'].__setitem__(1, f['variants'][0]['state']['shared_identities'][0]))

    def test_unresolved_retry_record_cannot_be_dropped(self):
        self.rejects(lambda f: f['fine_target_control'][-1]['target'].__setitem__(0, 0x12345678))

    def test_fine_target_cannot_alias_a_physical_recruit(self):
        def mutate(f):
            rows = f['fine_target_control']
            commit = copy.deepcopy(rows[-2])
            commit['mover'] = next(r['mover'] for r in rows if r.get('category') == 202)
            rows.insert(-1, commit)
        self.rejects(mutate)

    def test_recorded_captain_owner_is_required(self):
        self.rejects(lambda f: f['fine_target_control'].__setitem__(slice(None),
                     [r for r in f['fine_target_control'] if r.get('rawcode') != 0]))

    def test_blocker_slot_is_preserved_beside_target_pointer(self):
        rows = copy.deepcopy(self.fixture['fine_target_control'])
        rows[-1]['target'][1] = 123
        self.assertEqual(canonical_lifecycle(rows)[0]['target'], [-1, 123])

    def test_last_binding_cancellation_remains_separate(self):
        self.rejects(lambda f: f.__setitem__('last_reference_cancellation_remains_open', False))

    def test_complete_scene_cannot_claim_complete_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
