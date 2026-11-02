"""Reject incomplete modifier movement and weakened native removal contracts."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_speed_modifiers_trace import verify_contract,render_header

class SpeedModifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-speed-modifiers-1.27.json').read_text())
    def test_complete_native_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_speed_modifiers.h').read_text(),render_header(self.fixture))
    def test_complete_lifetimes_required(self):
        for key in('motion','states','routes','markers','buff_markers'):
            s=copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_changed_committed_velocity_rejected(self):
        s=copy.deepcopy(self.fixture);s['motion'][200][4]^=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_physical_only_must_preserve_magic_buff(self):
        s=copy.deepcopy(self.fixture);r=next(r for r in s['modifiers']if r['event']=='modifier-filter-end'and r['args'][0]);r['count']=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_magic_and_physical_argument_order_preserved(self):
        s=copy.deepcopy(self.fixture);r=next(r for r in s['modifiers']if r['event']=='modifier-filter-end'and r['args'][1]);r['args'][0],r['args'][1]=r['args'][1],r['args'][0]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_restored_speed_must_match(self):
        s=copy.deepcopy(self.fixture);s['modifiers'][-2]['output']=0
        # Change a witnessed effective-speed call, not a volatile observer field.
        r=next(r for r in reversed(s['modifiers'])if r['event']=='modifier-speed-effective');r['output']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_main_and_caster_must_both_be_accounted(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['motion']if r[0]==1)[0]=0
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
