"""Captain evidence must retain the complete native private handoff journey."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/frida'))
from verify_wc3_captain_home_trace import verify_producer, render_header


class CaptainHomeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-home-1.27.json').read_text())

    def spec(self, name='home'):
        return copy.deepcopy(self.fixture['journeys'][name])

    def test_home_admission_and_existing_roster_retention_repeat(self):
        for spec in self.fixture['journeys'].values(): verify_producer(spec)
        self.assertEqual(len(self.fixture['cases']), 6)

    def test_rejected_recruit_cannot_certify_movement(self):
        s = self.spec(); s['producer']['markers'][-1] = 'PATHCAPTAIN home rejected'
        with self.assertRaises(ValueError): verify_producer(s)

    def test_init_must_keep_the_existing_member(self):
        s = self.spec('init'); s['producer']['markers'][-1] = 'PATHCAPTAIN roster after init wrong'
        with self.assertRaises(ValueError): verify_producer(s)

    def test_missing_private_handoff_cannot_be_hidden(self):
        s = self.spec(); s['producer']['prepared'].clear()
        with self.assertRaises(ValueError): verify_producer(s)

    def test_private_point_request_must_bind_shared_parameters(self):
        s = self.spec(); s['producer']['prepared'][0]['bindShared'] = 0
        with self.assertRaises(ValueError): verify_producer(s)

    def test_virtual_and_physical_ranges_have_distinct_producers(self):
        s = self.spec(); s['producer']['ranges'][1][0] = s['producer']['ranges'][0][0]
        with self.assertRaises(ValueError): verify_producer(s)

    def test_whole_recruit_reference_is_not_truncated_to_engine_admission(self):
        f = self.fixture
        self.assertEqual(len(f['motion']), 178)
        self.assertEqual(f['engine_admission_commits'], 178)
        self.assertEqual(f['engine_admission_end_msec'], 2000)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_home.h').read_text(), render_header(f))

    def test_order_must_naturally_finish(self):
        s = self.spec(); s['producer']['samples'][-1] = s['producer']['samples'][-1].replace('order=0', 'order=851986')
        with self.assertRaises(ValueError): verify_producer(s)

    def test_recruit_count_and_rawcode_are_public_inputs(self):
        s = self.spec(); s['producer']['recruits'][0][0] = 13
        with self.assertRaises(ValueError): verify_producer(s)

    def test_create_captains_must_precede_authored_home_and_recruit(self):
        s = self.spec(); s['producer']['calls'].remove('create')
        with self.assertRaises(ValueError): verify_producer(s)

    def test_fresh_create_is_not_full_without_init(self):
        s = self.spec('full'); s['producer']['markers'][1] = 'PATHCAPTAIN full create wrong'
        with self.assertRaises(ValueError): verify_producer(s)

    def test_shortage_must_clear_full(self):
        s = self.spec('full'); s['producer']['markers'][-2] = 'PATHCAPTAIN full shortage wrong'
        with self.assertRaises(ValueError): verify_producer(s)

    def test_init_retry_sets_full_while_retaining_one_member(self):
        s = self.spec('full'); s['producer']['markers'][-1] = 'PATHCAPTAIN full retry retained wrong'
        with self.assertRaises(ValueError): verify_producer(s)


if __name__ == '__main__': unittest.main()
