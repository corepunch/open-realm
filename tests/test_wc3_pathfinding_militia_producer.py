import base64
from collections import Counter
import copy
import gzip
import json
import unittest
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import militia_producer_evidence as v

class MilitiaEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def change(self,edit,index=0,kind='accepted'):
        d=copy.deepcopy(self.bundle);c=d[kind]['captures'][index]
        rows=[json.loads(line) for line in v.literal(c).splitlines()]
        edit(rows)
        for row in rows:
            if row.get('event')=='trace-end':row['counts']=dict(Counter(r['event'] for r in rows if r['event'] in row['counts']))
        raw=b''.join((json.dumps(r)+'\n').encode() for r in rows)
        c.update(raw_b64=base64.b64encode(raw).decode(),raw_sha256=v.digest(raw))
        return d
    def event_change(self,event,key,value,scene=0):
        def edit(rows):next(r for r in rows if r.get('event')==event and r.get('scene')==scene and r.get('tick')==scene*500+100 and (event!='target-task' or r.get('caller')==0x62e9ce))[key]=value
        return edit
    def rejects(self,pattern,edit,**kw):
        with self.assertRaisesRegex(ValueError,pattern):v.audit(self.change(edit,**kw))
    def test_complete(self):self.assertEqual(v.audit()['public_markers'],1524)
    def test_owned(self):self.rejects('provenance',lambda r:r[0].update(owned=False))
    def test_environment(self):self.rejects('environment',lambda r:r[0].update(display=':97'))
    def test_source_pin(self):self.rejects('source pins',lambda r:r[0]['source_sha256'].update(map='0'*64))
    def test_footer(self):self.rejects('footer',lambda r:r[-1].update(complete=False))
    def test_trace_footer(self):self.rejects('completion',lambda r:r.pop(-2))
    def test_error(self):self.rejects('failed observer',lambda r:r.insert(1,{'event':'script-error'}))
    def test_control_hooks(self):self.rejects('control contains',lambda r:r.insert(1,{'event':'marker'}),index=2)
    def test_order(self):self.rejects('order/class',self.event_change('militia-producer-enter','order',852072))
    def test_class(self):self.rejects('order/class',self.event_change('militia-producer-enter','vtable',0))
    def test_authored(self):self.rejects('authored range',self.event_change('militia-authored-range','value',0))
    def test_far_range(self):self.rejects('range decision',self.event_change('target-range-query','range',0))
    def test_facing_window(self):self.rejects('facing decision',self.event_change('facing-query','window',0,scene=1))
    def test_facing_max(self):self.rejects('physical task/range',self.event_change('target-task','range',v.RANGE,scene=2))
    def test_missing_producer(self):self.rejects('producer cardinality',lambda rows:rows.pop(next(i for i,r in enumerate(rows) if r.get('event')=='militia-producer-enter' and r.get('scene')==0)))
    def test_observer_markers(self):self.rejects('observer/public',lambda rows:next(r for r in rows if r.get('event')=='marker').update(value='MIL changed'))
    def test_coordinated_public_mutation(self):
        d=copy.deepcopy(self.bundle)
        for c in d['accepted']['captures']:
            old=v.literal(c,'preload');new=old.replace(b'x=704.000',b'x=704.001',1)
            c.update(preload_b64=base64.b64encode(new).decode(),preload_sha256=v.digest(new))
            rows=[json.loads(line) for line in v.literal(c).splitlines()]
            for r in rows:
                if r.get('event')=='marker' and 'x=704.000' in r.get('value',''):
                    r['value']=r['value'].replace('x=704.000','x=704.001',1);break
            rows[-1]['sha256']=v.digest(new)
            raw=b''.join((json.dumps(r)+'\n').encode() for r in rows)
            c.update(raw_b64=base64.b64encode(raw).decode(),raw_sha256=v.digest(raw))
        with self.assertRaisesRegex(ValueError,'literal public projection'):v.audit(d)
    def test_rejected_not_accepted(self):
        d=copy.deepcopy(self.bundle);d['accepted']=d['rejected_wrong_names']
        with self.assertRaisesRegex(ValueError,'map pin'):v.audit(d)
    def test_no_engine_parity(self):
        d=copy.deepcopy(self.bundle);d['engine_parity']=True
        with self.assertRaisesRegex(ValueError,'contract'):v.audit(d)
    def test_literal_hash(self):
        d=copy.deepcopy(self.bundle);d['accepted']['map']['raw_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'literal hash'):v.audit(d)

if __name__=='__main__':unittest.main()
