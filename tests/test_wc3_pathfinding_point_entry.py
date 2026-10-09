import copy
import json
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
import base191_verify as oracle


class PointEntry(unittest.TestCase):
    def rows(self):
        rows=[]
        for i,(producer,world,fine)in enumerate([('jass',0,0x3efae148),('jass',0,0x3efae148),
                ('captain',0x43fa0000,0x417a0000),('captain',0x43480000,0x40c80000),('captain',0x43480000,0x40c80000)]):
            root=i*3+1;point=[0x44800000,0x44800000]
            begin=dict(event=producer+'-begin',id=root,parent=0,point=point.copy(),order=851986,range=world)
            rows.append(begin);parent=root
            if producer=='jass':rows.append(dict(event='point-task-begin',id=root+1,parent=root));parent=root+1
            bridge=root+2
            rows.extend([dict(event='bridge',id=bridge,parent=parent,producer='point-task'if producer=='jass'else producer,
                              caller=0x5ffdb4 if producer=='jass'else 0x9d4580,point=point.copy(),range=world,actorBridge=True,events=[0xd0196,0xd0198],flag=1),
                         dict(event='range-publish',bridge=bridge,value=fine,caller=0x05bb60),dict(event='bridge-end',id=bridge)])
            if producer=='jass':rows.append(dict(event='point-task-end',id=parent))
            rows.append(dict(event=producer+'-end',id=root,result=1))
        return rows

    def test_both_complete_publishers(self):
        self.assertEqual(oracle.timeline(self.rows()),self.rows())

    def test_rejects_unbalanced_and_missing_publication(self):
        for event in ('bridge-end','range-publish'):
            rows=self.rows();rows.pop(next(i for i,r in enumerate(rows)if r['event']==event))
            with self.assertRaises(ValueError):oracle.timeline(rows)

    def test_rejects_range_flag_and_virtual_actor_changes(self):
        for field,value,producer in [('flag',0,'point-task'),('range',1,'point-task'),('actorBridge',False,'captain')]:
            rows=self.rows();next(r for r in rows if r['event']=='bridge'and r['producer']==producer)[field]=value
            with self.assertRaises(ValueError):oracle.timeline(rows)

    def test_rejects_corrupted_zero_range_and_jass_destination(self):
        for event,field,value in [('range-publish','value',0),('bridge','point',[0,0])]:
            rows=self.rows();next(r for r in rows if r['event']==event)[field]=value
            with self.assertRaises(ValueError):oracle.timeline(rows)


if __name__=='__main__':unittest.main()
