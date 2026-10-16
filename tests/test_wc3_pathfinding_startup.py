import copy
import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('startup171',ROOT/'tools/ghidra/research/verify_startup171_seed.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)


class StartupEvidenceTests(unittest.TestCase):
    def rows(self):
        return [dict(event='metadata',binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',mode='observe',owned=True),
            dict(event='setup-record-tick',tick=123),dict(event='pref-bool',index=0x6a,result=0),
            dict(event='setup-seed-decision',owner=[1768977253,2822785048],flags=0,lobbySeed=123),
            dict(event='separation-visit'),dict(event='marker',value='NUMRNG label=complete'),
            dict(event='trace-end',installed=True)]

    def test_complete_requires_installed_end_and_completed_movement(self):
        v.verify_rows(self.rows(),True)
        for event in ('trace-end','marker','separation-visit'):
            with self.subTest(event=event),self.assertRaises(AssertionError):
                v.verify_rows([x for x in self.rows() if x['event']!=event],True)

    def test_incomplete_is_only_accepted_when_explicitly_labelled(self):
        rows=self.rows()[:4];v.verify_rows(rows,False)
        with self.assertRaises(AssertionError):v.verify_rows(rows,True)

    def test_host_record_and_lock_preference_must_agree(self):
        for event,field,value in [('setup-record-tick','tick',124),('pref-bool','result',1),
                                  ('setup-seed-decision','owner',[0,0]),('metadata','mode','control')]:
            rows=copy.deepcopy(self.rows());next(x for x in rows if x['event']==event)[field]=value
            with self.subTest(field=field),self.assertRaises(AssertionError):v.verify_rows(rows,True)

    def test_observer_failure_is_never_hidden_as_incomplete(self):
        with self.assertRaises(AssertionError):v.verify_rows(self.rows()+[dict(event='error')],False)


if __name__=='__main__':unittest.main()
