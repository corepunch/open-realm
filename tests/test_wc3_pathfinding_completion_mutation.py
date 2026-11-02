import copy
import json
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
import completion194_verify as V


class CompletionMutation(unittest.TestCase):
    def rows(self):
        return json.loads((ROOT/'tools/ghidra/fixtures/retail-completion-callback194-1.27.json').read_text())['timeline']

    def test_complete_actual_public_callback(self):
        self.assertEqual(V.validate(self.rows()),1140)

    def test_rejects_mutation_outside_owner_or_early_new_group(self):
        rows=self.rows();marker=next(r for r in rows if r['event']=='marker'and 'channel-before-mutation'in r['value'])
        marker['insideOwner']=False
        with self.assertRaises(ValueError):V.validate(rows)
        rows=self.rows();marker=next(r for r in rows if r['event']=='marker'and 'channel-after-mutation'in r['value'])
        new=marker['groups'][0]
        rows.insert(rows.index(marker)+1,dict(event='group-begin',c=1140,group=copy.deepcopy(new)))
        with self.assertRaises(ValueError):V.validate(rows)

    def test_rejects_early_release_and_stale_peer(self):
        for kind in ('completed','peer'):
            rows=self.rows();end=next(r for r in rows if r['event']=='owner-end'and r['c']==1140)
            if kind=='completed':end['groups'].pop()
            else:
                before=next(r for r in rows if r['event']=='marker'and 'channel-before-mutation'in r['value'])
                end['groups'].append(copy.deepcopy(before['groups'][1]))
            with self.assertRaises(ValueError):V.validate(rows)

    def test_rejects_stale_member_and_incomplete_lifetimes(self):
        rows=self.rows();next(r for r in rows if r['event']=='finished-member')['member'][5]=True
        with self.assertRaises(ValueError):V.validate(rows)
        rows=self.rows();rows.pop(next(i for i,r in enumerate(rows)if r['event']=='owner-end'))
        with self.assertRaises(ValueError):V.validate(rows)


if __name__=='__main__':unittest.main()
