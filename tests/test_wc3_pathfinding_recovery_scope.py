"""Reject incomplete or leaked original recovery-scope evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('recovery_scope',ROOT/'tools/ghidra/verify_wc3_pathing_recovery_scope.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class RecoveryScopeEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.report=json.loads((ROOT/'tools/ghidra/fixtures/retail-recovery-scope183-1.27.json').read_text())

    def test_complete_original_exit_matrix(self):
        self.assertIs(module.validate_report(self.report),self.report)

    def test_omitted_or_duplicated_exit_is_rejected(self):
        for duplicate in (False,True):
            report=copy.deepcopy(self.report)
            if duplicate:report['cases'][-1]=report['cases'][0]
            else:report['cases'].pop()
            with self.subTest(duplicate=duplicate),self.assertRaises(ValueError):module.validate_report(report)

    def test_early_release_and_counter_or_mode_leaks_are_rejected(self):
        for change in ('held','released','mode','publication'):
            report=copy.deepcopy(self.report)
            row=next(r for r in report['cases']if r['exit']=='admitted'and not r['absent'])
            if change=='held':row['trace'][-1][1]=row['outer']
            elif change=='released':row['final_counter']+=1
            elif change=='mode':row['final_mode']=1
            else:row['trace'].pop()
            with self.subTest(change=change),self.assertRaises(ValueError):module.validate_report(report)


if __name__=='__main__':unittest.main()
