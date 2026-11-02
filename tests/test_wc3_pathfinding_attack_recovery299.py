"""Reject damaged native299 streams without running the game or external tools."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
_spec=importlib.util.spec_from_file_location('attack299',ROOT/'tools/ghidra/research/attack_recovery299_evidence.py')
evidence=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(evidence)

class Native299Evidence(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.files,cls.spec=evidence.load()
 def change(self,mutation,name='observe2'):
  files=dict(self.files)
  rows=[json.loads(s) for s in files[name+'.jsonl'].decode().splitlines()]
  mutation(rows)
  files[name+'.jsonl']=('\n'.join(json.dumps(r) for r in rows)+'\n').encode()
  # Deliberately bypass member hashing to exercise the semantic guard itself.
  with self.assertRaises(AssertionError):evidence.audit(files,self.spec)
 def test_literal_archive_and_complete_streams(self):
  self.assertTrue(evidence.audit(self.files,self.spec)['native_pointer_only_equal'])
 def test_one_raw_clock_word_cannot_be_normalized(self):
  def mutate(rows):
   next(r for r in rows if r.get('event')=='arrival')['clock']['primary'][0]^=1
  self.change(mutate)
 def test_one_heading_word_cannot_be_normalized(self):
  def mutate(rows):next(r for r in rows if r.get('event')=='velocity')['heading']^=1
  self.change(mutate)
 def test_reordered_rows_rejected(self):
  def mutate(rows):
   i=next(i for i,r in enumerate(rows) if r.get('event')=='arrival')
   rows[i],rows[i+1]=rows[i+1],rows[i]
  self.change(mutate)
 def test_missing_row_rejected(self):
  def mutate(rows):rows.pop(next(i for i,r in enumerate(rows) if r.get('event')=='arrival'))
  self.change(mutate)
 def test_wrong_overwritten_event_payload_rejected(self):
  def mutate(rows):next(r for r in rows if isinstance(r.get('event'),list))['event'][2]^=1
  self.change(mutate)
 def test_fabricated_payload_unit_pointer_rejected(self):
  def mutate(rows):next(r for r in rows if isinstance(r.get('event'),list))['event'][3]+=4
  self.change(mutate)
 def test_extra_control_hook_rejected(self):
  self.change(lambda rows:rows.append({'event':'arrival'}),'control1')
 def test_footer_drop_rejected(self):
  def mutate(rows):next(r for r in rows if r.get('event')=='trace-end')['dropped']=1
  self.change(mutate)
 def test_frozen_source_mismatch_rejected(self):
  files=dict(self.files);files['sources/attack_recovery299_probe.j']+=b'\n'
  with self.assertRaises(AssertionError):evidence.audit(files,self.spec)

if __name__=='__main__':unittest.main()
