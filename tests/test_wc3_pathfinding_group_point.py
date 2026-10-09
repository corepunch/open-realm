import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('group_point_trace', ROOT/'tools/frida/verify_wc3_group_point_trace.py')
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class GroupPointTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-group-point-admission-1.27.json').read_text())
        self.rows = [dict(event='metadata', pid=123, **self.fixture['metadata']),
                     dict(event='marker', value='PATHTRACE tick=0 label=start_group_orders x=0'),
                     dict(event='marker', value='PATHTRACE tick=300 label=complete x=0')]
        for n, batch in enumerate(self.fixture['admissions']):
            for row in batch:
                r = dict(row)
                if 'member' in r:
                    r['unit'] = hex(0x1000 + r.pop('member')*32)
                    r['request'] = hex(0x2000+n*32)
                self.rows.append(r)
        for tick, form in zip(MOD.TICKS, MOD.FORMS):
            self.rows.append(dict(event='group-order-marker', value=f'PATHGROUP tick={tick} form={form} accepted=1'))
            for i in range(14):
                order = 851986 if i < 12 else 0
                self.rows.append(dict(event='group-order-marker', value=f'PATHGROUP tick={tick} member={i} handle={100+i} order={order} x=0.000 y=0.000'))
        self.rows.append(dict(event='trace-end'))

    def test_original_admission_contract(self):
        result = MOD.verify(self.rows, self.fixture)
        self.assertEqual(result['admitted_members'], 48)
        self.assertEqual(result['excluded_members'], 8)
        self.assertEqual(result['shared_requests'], 4)

    def test_rejects_missing_admission_and_changed_words(self):
        for kind in ('missing', 'word', 'order', 'snapshot', 'shared', 'provenance', 'end', 'error'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(self.rows)
                member = next(r for r in rows if r.get('event') == 'group-point-member-begin')
                if kind == 'missing': rows.remove(member)
                elif kind == 'word': member['point'][0] ^= 1
                elif kind == 'order': member['phase'] = 'admit'
                elif kind == 'snapshot': member['unit'] = '0x9000'
                elif kind == 'shared': member['request'] = '0x9000'
                elif kind == 'provenance': rows[0]['source_sha256']['wc3_pathfinding.js'] = '0'*64
                elif kind == 'end': rows.pop()
                else: rows.append(dict(type='error', description='observer failed'))
                with self.assertRaises(ValueError): MOD.verify(rows, self.fixture)

    def test_rejects_extra_native_member(self):
        row = next(r for r in self.rows if r.get('event') == 'group-order-marker' and 'tick=10 member=13 ' in r['value'])
        row['value'] = row['value'].replace('order=0', 'order=851986')
        with self.assertRaises(ValueError): MOD.verify(self.rows, self.fixture)


if __name__ == '__main__': unittest.main()
