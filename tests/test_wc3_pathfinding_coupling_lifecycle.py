import copy,gzip,json,unittest
from collections import Counter
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import coupling_lifecycle_evidence as v
class CouplingLifecycleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()));cls.data=v.unpack(cls.bundle)
 def change(self,fn):
  d=copy.deepcopy(self.data);rows=d['captures'][0]['rows'];fn(rows)
  for r in rows:
   if r['event']=='trace-end':r['counts']=dict(Counter(x['event'] for x in rows if x['event'] in r['counts']))
  return d
 def row(self,rows,event,scene=0):return next(r for r in rows if r['event']==event and r.get('scene')==scene)
 def reject(self,message,fn):
  with self.assertRaisesRegex(ValueError,message):v.audit_loaded(self.change(fn))
 def test_complete(self):
  r=v.audit();self.assertEqual(r['matching_public_markers'],1791);self.assertFalse(r['engine_parity']);self.assertEqual(len(r['scenes']),11)
 def test_initiator_resume_recreates_pair(self):
  def edit(rows):
   p=[r for r in rows if r['event']=='couple-producer-enter' and r.get('scene')==7];p[-1]['order']=852143
  self.reject('resume distinction',edit)
 def test_reciprocal_resume_stays_internal(self):
  def edit(rows):
   p=[r for r in rows if r['event']=='couple-producer-enter' and r.get('scene')==8];p[-1]['order']=852143
  self.reject('resume distinction',edit)
 def test_zero_range(self):self.reject('zero raw',lambda rows:self.row(rows,'target-task').update(range=0x43a68000))
 def test_opposite_partner(self):self.reject('opposing task',lambda rows:self.row(rows,'target-task').update(target='0x0'))
 def test_ready_generation(self):self.reject('ready incarnation',lambda rows:self.row(rows,'couple-ready').update(partner=[0,0]))
 def test_ready333(self):self.reject('ready incarnation/range',lambda rows:self.row(rows,'couple-ready').update(range=0))
 def test_ready_window_precedes_stop(self):
  def edit(rows):
   i=next(i for i,r in enumerate(rows) if r['event']=='couple-ready' and r.get('scene')==9);row=rows.pop(i)
   j=next(i for i,r in enumerate(rows) if r['event']=='marker' and r.get('scene')==9 and 'label=before-interrupt' in r['value']);rows.insert(j+1,row)
  self.reject('observed ready window',edit)
 def test_ready_before_merge(self):
  def edit(rows):
   i=next(i for i,r in enumerate(rows) if r['event']=='couple-merge');m=rows.pop(i);j=next(i for i,r in enumerate(rows) if r['event']=='couple-ready');rows.insert(j,m)
  self.reject('two ready before merge',edit)
 def test_control_unhooked(self):
  d=copy.deepcopy(self.data);d['captures'][2]['rows'].insert(1,{'event':'velocity'})
  with self.assertRaisesRegex(ValueError,'unhooked'):v.audit_loaded(d)
 def test_footer(self):self.reject('completion footer',lambda rows:rows[-1].update(complete=False))
 def test_authored_override(self):
  d=copy.deepcopy(self.data);d['w3a']=d['w3a'][:-1]
  with self.assertRaisesRegex(ValueError,'authored333'):v.audit_loaded(d)
 def test_scope(self):
  d=copy.deepcopy(self.bundle);d['engine_parity']=True
  with self.assertRaisesRegex(ValueError,'scope'):v.audit(d)
 def test_capture_literal(self):
  d=copy.deepcopy(self.bundle);d['captures'][0]['raw_sha256']='0'*64
  with self.assertRaisesRegex(ValueError,'literal digest'):v.audit(d)

 def test_fixed_compressed_archive_digest(self):
  from unittest.mock import Mock,patch
  with patch.object(v,'BUNDLE',Mock(read_bytes=lambda:b'altered')):
   with self.assertRaisesRegex(ValueError,'compressed bundle digest'):v.audit()

if __name__=='__main__':unittest.main()
