"""A completed yield capture must retain provenance, observation and control boundaries."""
import copy
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from research.verify_yield_lifecycle162_live import DLL,validate_rows


class YieldLifecycleEvidence(unittest.TestCase):
    def observed(self):
        return [dict(event='metadata',task='ROUTE-05.1',mode='observe',sha256=DLL,owned=True),
                dict(event='marker',value='ROUTE05 complete'),
                dict(event='trace-end',installed=True,finished=True),
                dict(event='preload-file',complete=True,markers=1)]

    def test_completed_observation_is_accepted(self):
        markers,_=validate_rows(self.observed(),'observe')
        self.assertEqual(markers,['ROUTE05 complete'])

    def test_missing_simulation_completion_is_rejected(self):
        rows=self.observed();rows[-1]['complete']=False
        with self.assertRaisesRegex(ValueError,'incomplete public'):validate_rows(rows,'observe')

    def test_late_or_unfinished_observer_is_rejected(self):
        for field in ('installed','finished'):
            rows=self.observed();rows[-2][field]=False
            with self.assertRaisesRegex(ValueError,'incomplete observer'):validate_rows(rows,'observe')

    def test_wrong_binary_or_unowned_process_is_rejected(self):
        for field,value in [('sha256','wrong'),('owned',False),('task','different')]:
            rows=self.observed();rows[0][field]=value
            with self.assertRaisesRegex(ValueError,'provenance'):validate_rows(rows,'observe')

    def test_failed_hook_is_rejected(self):
        rows=self.observed();rows.insert(1,dict(type='error',description='hook failed'))
        with self.assertRaisesRegex(ValueError,'failed capture'):validate_rows(rows,'observe')

    def test_marker_loss_is_rejected(self):
        rows=self.observed();rows.pop(1)
        with self.assertRaisesRegex(ValueError,'marker count'):validate_rows(rows,'observe')

    def test_duplicate_provenance_is_rejected(self):
        rows=self.observed();rows.insert(1,copy.deepcopy(rows[0]))
        with self.assertRaisesRegex(ValueError,'one leading'):validate_rows(rows,'observe')

    def test_control_must_have_no_injected_observations(self):
        rows=self.observed();rows[0]['mode']='control'
        with self.assertRaisesRegex(ValueError,'instrumented control'):validate_rows(rows,'control')
        validate_rows([rows[0],rows[-1]],'control')
