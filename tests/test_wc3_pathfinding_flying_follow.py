import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('follow186', ROOT / 'tools/frida/research/target186_engine_fixture.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class FlyingFollowEvidence(unittest.TestCase):
    def setUp(self):
        self.header = (ROOT / 'games/warcraft-3/game/tests/fixtures/retail_flying_follow186.h').read_text()
        self.rows = [[int(v, 16) for v in re.findall(r'0x([0-9a-f]+)u', line)]
                     for line in self.header.splitlines() if line.startswith('    {')]

    def stream(self):
        out = [dict(event='marker', value='T021 tick=1020 s=5 l=0 label=begin-setup')]
        for r in self.rows:
            out.append(dict(event='gtick', c=r[0], cd=r[1], unseen=r[2], flags=0x1000 | r[24], count=1, id=[1, 2],
                            path=dict(dest=r[3:5], times=r[5:7], cnt=[0, r[7]], idx=[-1, r[8]]),
                            members=[dict(m='0x100', grp=[1, 2], pos=r[9:11], vel=r[11:13], range=r[13],
                                          path=dict(dest=r[14:16], times=r[16:18], cnt=r[18:20],
                                                    idx=r[20:22], retry=r[22:24]))]))
        return out

    def test_frozen_header_is_coupled_to_all_raw_fields(self):
        rows = fixture.owner_rows(self.stream())
        self.assertEqual(rows, self.rows)
        self.assertEqual(fixture.render(rows), self.header)
        self.assertEqual(len(rows), 617)
        self.assertEqual(sum(not r[-1] for r in rows), 93)

    def test_missing_owner_visit_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            fixture.owner_rows(self.stream()[:-1])

    def test_empty_old_group_does_not_duplicate_handoff(self):
        stream = self.stream()
        stream.insert(94, dict(event='gtick', flags=0x1000, count=1, members=[dict(m=None)]))
        self.assertEqual(fixture.owner_rows(stream), self.rows)

    def test_detached_stale_member_is_not_a_live_owner_visit(self):
        stream = self.stream()
        stale = dict(stream[-1])
        stale['members'] = [dict(stale['members'][0], grp=[0xffffffff, 0xffffffff])]
        stream.append(stale)
        self.assertEqual(fixture.owner_rows(stream), self.rows)

    def test_other_scenes_do_not_enter_the_engine_fixture(self):
        stream = self.stream()
        stream += [dict(event='marker', value='T021 tick=1220 s=6 l=0 label=begin-setup'), stream[-1]]
        self.assertEqual(fixture.owner_rows(stream), self.rows)

    def test_changed_numeric_word_cannot_render_as_the_original(self):
        for field in range(25):
            rows = [r.copy() for r in self.rows]
            rows[200][field] ^= 1
            self.assertNotEqual(fixture.render(rows), self.header)


if __name__ == '__main__':
    unittest.main()
