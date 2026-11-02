"""Stock movement-profile observer completion and publication contract."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_profile_trace import verify, verify_table


class ProfileTraceTests(unittest.TestCase):
    def setUp(self):
        self.rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-ground-profile-1.27.json').read_text())['observations']

    def test_original_footman_profile_publishes_both_masks(self):
        result=verify(self.rows)
        self.assertEqual(result['rawcode'],0x68666f6f)
        self.assertEqual(result['category'],0xca)
        self.assertEqual(result['query_mask'],2)
        self.assertEqual(result['path_mask'],0x02000002)
        self.assertEqual(result['publications'],[[0x010000ca,0x02000002]]*2)

    def test_original_profile_table_covers_seven_authored_types(self):
        rows = json.loads((ROOT / 'tools/ghidra/fixtures/retail-movement-profiles-1.27.json').read_text())['observations']
        result = verify_table(rows)
        self.assertEqual(result['publications'], 14)
        self.assertEqual([(r['category'], r['query_mask']) for r in result['profiles']],
                         [(0, 0), (0xca, 64), (0xca, 2), (0, 4), (0xca, 2), (0xca, 2), (0xca, 128)])

    def test_truncation_errors_and_one_word_changes_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'completion'):verify(self.rows[:-1])
        with self.assertRaisesRegex(ValueError,'observer'):verify(self.rows+[{'type':'error'}])
        changed=copy.deepcopy(self.rows);changed.pop(1)
        with self.assertRaisesRegex(ValueError,'truncated'):verify(changed)
        for field in ('category','queryMask','objectCategory','pathMask'):
            changed=copy.deepcopy(self.rows)
            row=next(r for r in changed if r.get('event')=='movement-mask-publication')
            row[field]^=1
            with self.assertRaises(ValueError):verify(changed)

    def table_rows(self):
        rows = copy.deepcopy(self.rows)
        for row in rows:
            if row['event'] == 'movement-mask-publication':
                row['rawcode'] = 0x68666f6f
        added = []
        for row in rows:
            if row['event'] not in ('movement-profile', 'movement-mask-publication'):
                continue
            row = copy.deepcopy(row)
            row['rawcode'] = 0x68677279
            if row['event'] == 'movement-profile':
                row['value'] = 0 if row['kind'] == 'category' else 4
            else:
                row.update(category=0, queryMask=4, objectCategory=0x01000000, pathMask=0x04000004,
                           mover='0x5678')
            added.append(row)
        counts = rows[-1]['counts']
        for key in ('profile-category', 'profile-query-mask', 'movement-mask-publication'):
            counts[key] *= 2
        return rows[:-1] + added + rows[-1:]

    def test_profile_table_keeps_each_rawcode_publication_separate(self):
        result = verify_table(self.table_rows())
        self.assertEqual([(r['category'], r['query_mask']) for r in result['profiles']], [(0xca, 2), (0, 4)])

    def test_profile_table_rejects_wrong_rawcode_and_truncation(self):
        for mutation in ('rawcode', 'truncate', 'mover'):
            rows = self.table_rows()
            publication = next(r for r in rows if r['event'] == 'movement-mask-publication')
            if mutation == 'rawcode':
                publication['rawcode'] = 0x68677279
            elif mutation == 'truncate':
                rows.remove(publication)
            else:
                publication['mover'] = '0x5678'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                verify_table(rows)

    def fine_rows(self):
        # Controlled transport fixture: the live captures remain pinned in the
        # corpus. +88 deliberately stays nonzero while actual velocity stops.
        rows=[copy.deepcopy(r) for r in self.rows if r['event'] in
              ('metadata','movement-profile','movement-mask-publication','trace-end')]
        rows[0].update(blockers=True,velocityEvents=True)
        mover=next(r['mover'] for r in rows if r['event']=='movement-mask-publication')
        commits=[dict(event='velocity-commit',mover=mover,fineObject='0x1234',fineFlagsBefore=0,
                      fineFlagsAfter=0x20000000,after=[0,0,0,0,0x3f800000,0,0x41400000,0]),
                 dict(event='velocity-commit',mover=mover,fineObject='0x1234',fineFlagsBefore=0x20000000,
                      fineFlagsAfter=0,after=[0,0,0,0,0,0,0x41400000,0])]
        search=dict(event='search',kind='fine',blockers=dict(objectHits=1,omittedHits=0,unclassifiedHits=0,
            objects={'0x1234':dict(isMover=True,payload=mover,flags=0,mode=0,objectMask=0x010000ca,
                                  queryMask=0x02000002)}))
        rows[-1]['counts'].update({'velocity-commit':2,'fine-search':1})
        return rows[:-1]+commits+[search]+rows[-1:]

    def test_fine_object_velocity_uses_actual_vector_and_complete_idle_hits(self):
        result=verify(self.fine_rows(),True)
        self.assertEqual([result[k] for k in ('velocity_commits','moving_transitions','idle_transitions',
                                             'fine_searches','object_hits','object_records')],[2,1,1,1,1,1])

    def test_fine_object_truncation_flag_and_blocker_changes_are_rejected(self):
        for mutation in ('truncated','flag','missing-object','mode','mask','omitted'):
            rows=self.fine_rows()
            commits=[r for r in rows if r['event']=='velocity-commit']
            stats=next(r['blockers'] for r in rows if r['event']=='search')
            obj=next(iter(stats['objects'].values()))
            if mutation=='truncated':rows.remove(commits[-1])
            elif mutation=='flag':commits[-1]['fineFlagsAfter']=0x20000000
            elif mutation=='missing-object':commits[0].pop('fineObject')
            elif mutation=='mode':obj['mode']=1
            elif mutation=='mask':obj['objectMask']^=2
            else:stats['omittedHits']=1
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):verify(rows,True)


if __name__=='__main__':unittest.main()
