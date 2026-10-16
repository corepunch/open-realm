import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
import captain193_verify as V


class CaptainRetention(unittest.TestCase):
    def state(self):
        return json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-retained-range193-1.27.json').read_text())['timeline']

    def test_complete_retail_contract(self):
        V.validate_timeline(self.state())

    def test_requires_every_initial_range_and_point_request(self):
        for field in ('calls', 'points'):
            state = self.state(); state[field].pop()
            with self.assertRaises(ValueError): V.validate_timeline(state)
            state = self.state(); state[field].append(copy.deepcopy(state[field][0]))
            with self.assertRaises(ValueError): V.validate_timeline(state)

    def test_rejects_premature_refresh_or_replaced_head(self):
        for field in ('head', 'range'):
            state = self.state()
            points = [r for r in state['snapshots'] if r['reason'] == 'captain-point-after']
            rifle = next(r for r in points[2]['rows'] if r['rawcode'] == int.from_bytes(b'hRA9', 'big'))
            if field == 'head': rifle['head'][1] += 1
            else: rifle['mover']['storedRange'] = 0x41670000
            with self.assertRaises(ValueError): V.validate_timeline(state)

    def test_requires_actual_upgrade_and_all_member_retention(self):
        state = self.state(); points = [r for r in state['snapshots'] if r['reason'] == 'captain-point-after']
        rifle = next(r for r in points[2]['rows'] if r['rawcode'] == int.from_bytes(b'hRA9', 'big'))
        rifle['attack']['ranges'][0] = 0x43c80000
        with self.assertRaises(ValueError): V.validate_timeline(state)
        state = self.state(); points = [r for r in state['snapshots'] if r['reason'] == 'captain-point-after']
        points[2]['rows'][0]['head'][0] += 1
        with self.assertRaises(ValueError): V.validate_timeline(state)


if __name__ == '__main__': unittest.main()
