"""Stock movement-profile observer completion and publication contract."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_profile_trace import verify


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
