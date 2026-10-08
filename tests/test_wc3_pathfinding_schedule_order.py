import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
import schedule190_verify as oracle


class ScheduleOrder(unittest.TestCase):
    # Expand a complete policy timeline; identities and published words are
    # inputs to the validator, not a second implementation of Move.
    def rows(self):
        rows=[]
        for i in range(1000):
            c=i+1;rows.append(dict(event='owner-begin',c=i));rows.append(dict(event='scheduler',c=c))
            if i<32:
                for event in ['radius','group']:
                    for n,(identity,count)in enumerate([([1,20],1),([1,10],12)]):
                        params=[0x7f7fffff,0x7f7fffff if n==0 else 0x40960000,
                                0 if event=='radius'and n==0 else 0x3ffc0000]
                        rows.append(dict(event=event,c=c,id=identity,count=count,shared=[2,1],parameters=params))
                        if event=='group':
                            for stage in ['decide','commit']:
                                rows.append(dict(event=stage,c=c,id=identity,count=count,shared=[2,1],parameters=params.copy()))
            rows.append(dict(event='separate',c=c));rows.append(dict(event='owner-end',c=c))
        return rows

    def test_complete_joint_phase_sequence(self):
        self.assertEqual(len(oracle.timeline(self.rows())),1000)

    def test_requires_separation_and_completed_owner(self):
        for event in ['separate','owner-end']:
            rows=self.rows();rows.pop(next(i for i,r in enumerate(rows)if r['event']==event))
            with self.assertRaises(ValueError):oracle.timeline(rows)

    def test_rejects_slot_order_and_incomplete_aggregate(self):
        for field,value in [('id',[1,30]),('parameters',[0x7f7fffff,0x7f7fffff,0])]:
            rows=self.rows();second=[i for i,r in enumerate(rows)if r['event']=='radius'][1]
            rows[second][field]=value
            with self.assertRaises(ValueError):oracle.timeline(rows)

    def test_rejects_interleaved_radius_after_group_commit(self):
        rows=self.rows();first=[i for i,r in enumerate(rows)if r['event']=='group'][0]
        rows.insert(first+1,dict(event='radius',c=1,id=[1,9],count=1,shared=[2,1],parameters=[0,0,0]))
        with self.assertRaises(ValueError):oracle.timeline(rows)


if __name__=='__main__':unittest.main()
