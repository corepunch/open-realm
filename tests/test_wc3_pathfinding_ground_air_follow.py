import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('follow187', ROOT / 'tools/frida/research/follow187_engine_fixture.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class GroundAirFollowEvidence(unittest.TestCase):
    def setUp(self):
        self.header = (ROOT / 'games/warcraft-3/game/tests/fixtures/retail_ground_air_follow187.h').read_text()
        rows = [[int(v, 16) for v in re.findall(r'0x([0-9a-f]+)u', line)]
                for line in self.header.splitlines() if line.startswith('    {')]
        self.follower, self.target = rows[:616], rows[616:]

    def stream(self):
        out = [dict(event='marker', value='T021 tick=1420 s=7 l=0 label=begin-setup')]
        for r in self.follower:
            out.append(dict(event='gtick', c=r[0], cd=r[1], unseen=r[2], flags=0x1000 | r[24], count=1, id=[1, 2],
                            path=dict(dest=r[3:5], times=r[5:7], cnt=[0, r[7]], idx=[-1, r[8]]),
                            members=[dict(m='0x100', grp=[1, 2], pos=r[9:11], vel=r[11:13], range=r[13],
                                          path=dict(dest=r[14:16], times=r[16:18], cnt=r[18:20],
                                                    idx=r[20:22], retry=r[22:24]))]))
        for r in self.target:
            out.append(dict(event='gtick', c=r[0], flags=0, count=1, id=[3, 4],
                            members=[dict(m='0x200', grp=[3, 4], pos=r[1:3], vel=r[3:5])]))
        return out

    def regions(self):
        groups = [dict(event='group-path-regions', c=i, group='0x100', path='0x200', self='0x0', target='0x300')
                  for i in range(1239)]
        coarse = [dict(event='coarse-path-regions', c=i, path='0x200' if i < 40 else '0x400',
                       self='0x0' if i < 40 else '0x500', target='0x300') for i in range(73)]
        setters = [dict(event='set-self-region', c=i, path='0x400', region='0x500', caller=0x170ad9) for i in range(2)]
        return groups + coarse + setters

    def test_header_maps_every_follower_and_target_word(self):
        rows = fixture.owner_rows(self.stream())
        self.assertEqual(rows, (self.follower, self.target))
        self.assertEqual(fixture.render(*rows), self.header)

    def test_missing_target_visit_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            fixture.owner_rows(self.stream()[:-1])

    def test_detached_member_and_old_empty_group_are_excluded(self):
        stream = self.stream()
        stale = dict(stream[1], members=[dict(stream[1]['members'][0], grp=[0xffffffff, 0xffffffff])])
        stream.insert(1, stale)
        stream.insert(1, dict(event='gtick', count=1, members=[dict(m=None)]))
        self.assertEqual(fixture.owner_rows(stream), (self.follower, self.target))

    def test_zero_sign_is_part_of_the_frozen_velocity(self):
        self.assertEqual(self.follower[396][11], 0x80000000)
        changed = [r.copy() for r in self.follower]
        changed[396][11] = 0
        self.assertNotEqual(fixture.render(changed, self.target), self.header)

    def test_group_and_member_regions_remain_distinct(self):
        self.assertEqual(len(fixture.region_rows(self.regions())), 1314)

    def test_group_self_region_is_rejected(self):
        rows = self.regions()
        rows[0]['self'] = '0x500'
        with self.assertRaisesRegex(ValueError, 'group path acquired'):
            fixture.region_rows(rows)

    def test_missing_member_self_region_is_rejected(self):
        rows = self.regions()
        rows[1239 + 40]['self'] = '0x0'
        with self.assertRaisesRegex(ValueError, 'coarse self region'):
            fixture.region_rows(rows)

    def test_setter_producer_and_complete_boundary_counts_are_required(self):
        rows = self.regions()
        rows[-1]['caller'] ^= 1
        with self.assertRaisesRegex(ValueError, 'unexpected producer'):
            fixture.region_rows(rows)
        with self.assertRaisesRegex(ValueError, 'incomplete region'):
            fixture.region_rows(rows[:-1])

    def test_address_normalization_preserves_region_identity_relations(self):
        rows = self.regions()
        moved = [{k: (hex(int(v, 16) + 0x10000) if isinstance(v, str) and v.startswith('0x') and v != '0x0' else v)
                  for k, v in r.items()} for r in rows]
        self.assertEqual(fixture.region_rows(rows), fixture.region_rows(moved))
        moved[-1]['region'] = '0x90000'
        self.assertNotEqual(fixture.region_rows(rows), fixture.region_rows(moved))


if __name__ == '__main__':
    unittest.main()
