"""Range departure cannot be confused with failed-path recovery or full roster parity."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_departure_trace import verify_contract


class CaptainDepartureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-departure-1.27.json').read_text())

    def rejects(self,mutate):
        f=copy.deepcopy(self.fixture);mutate(f)
        with self.assertRaises(ValueError):verify_contract(f)

    def test_observed_membership_and_private_reissue(self):
        verify_contract(self.fixture)

    def test_inner_departure_cannot_change_outer_count(self):
        self.rejects(lambda f:next(r for r in f['membership_state']['counters'] if r[3]==1 and r[4]==-1).__setitem__(3,0))

    def test_departure_retains_exact_deadline(self):
        self.rejects(lambda f:f['membership_state']['counters'][-3][0].__setitem__(0,0x41400000))

    def test_departure_reissues_the_real_birth(self):
        self.rejects(lambda f:f['membership_state']['reissues'][-1].__setitem__(2,1))

    def test_private_group_reuses_slot_with_new_generation(self):
        self.rejects(lambda f:f['membership_state']['bindings'][-1].__setitem__(1,[1820,2123]))

    def test_reissue_consumes_old_velocity_before_stopping(self):
        self.rejects(lambda f:f['membership_state']['stop']['after'].__setitem__(4,0x3f800000))

    def test_private_approach_keeps_authored_attack_range(self):
        self.rejects(lambda f:f['membership_state']['arrival'].__setitem__('storedRange',0x3efae148))

    def test_second_shared_generation_cannot_be_claimed(self):
        for key,value in [('whole_engine_parity',True),('second_shared_generation_remains_open',False),('engine_prefix_commits',5462)]:
            self.rejects(lambda f:f.__setitem__(key,value))


if __name__=='__main__':unittest.main()
