"""Full original twelve-member words; engine trajectory remains an independent normal-frame test."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest
from test_wc3_pathfinding_public_pair import capture_rows

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_public_twelve_trace import verify_twelve


class PublicTwelveTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-twelve-1.27.json').read_text())
        self.rows=capture_rows(self.fixture,[hex(0x1000+i*0x1000) for i in range(12)],'twelve-marker')

    def test_complete_original_phases_and_public_samples(self):
        result=verify_twelve(self.rows,self.fixture)
        self.assertEqual(result['group_owner_passes'],297)
        self.assertEqual(result['natural_arrivals'],12)
        self.assertEqual(result['phase_sha256'],'2c763518948c55afb9c0f0bed25c3eec69f5a2763626ff5e48a0aeb1f2bcf977')

    def test_phase_order_member_swap_and_samples_cannot_change(self):
        for kind in ('pose','member','flags','destination','velocity','phase','missing','sample'):
            with self.subTest(kind=kind):
                rows=copy.deepcopy(self.rows)
                phase=next(r for r in rows if r['event']=='pair-group-phase-end' and r['phase']=='decide')
                if kind=='pose': phase['members'][0]['pose'][2]^=1
                elif kind=='member': phase['members'][0]['mover']='0x2000'
                elif kind=='flags': phase['members'][0]['row'][10]^=1
                elif kind=='destination': phase['members'][0]['row'][6]^=1
                elif kind=='velocity': next(r for r in rows if r['event']=='velocity-commit')['after'][4]^=1
                elif kind=='phase': phase['phase']='commit'
                elif kind=='missing': rows.remove(phase)
                else: next(r for r in rows if r['event']=='twelve-marker')['value']='PATHDOZEN tick=10 accepted=0'
                with self.assertRaises(ValueError): verify_twelve(rows,self.fixture)

    def test_engine_fixture_contains_every_original_commit_in_order(self):
        header=(ROOT/'games/warcraft-3/game/tests/retail_public_twelve.h').read_text()
        actual=[[int(v.rstrip('u'),16) for v in re.findall(r'0x[0-9a-f]+u',line)]
                for line in header.splitlines() if line.lstrip().startswith('{0x')]
        self.assertEqual(actual,self.fixture['engine_motion'])


if __name__=='__main__': unittest.main()
