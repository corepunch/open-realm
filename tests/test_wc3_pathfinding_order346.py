import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_order346 as oracle


class ArrivalFailureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(oracle.SPEC.read_text())
        self.files = json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))

    def change(self, names, predicate, edit):
        for name in names:
            rows = [json.loads(line) for line in self.files[name].splitlines()]
            for row in rows:
                if predicate(row):
                    edit(row)
            self.files[name] = '\n'.join(json.dumps(row) for row in rows)+'\n'
            self.spec['files'][name] = oracle.digest(self.files[name].encode())

    def reject(self):
        with self.assertRaises(ValueError):
            oracle.validate(self.spec,self.files)

    def test_accepted_captures(self):
        result = oracle.validate(self.spec,self.files)
        self.assertEqual(result['captures'],69)
        self.assertTrue(result['positive_point_load'])

    def test_archived_semantic_contracts(self):
        self.assertEqual(len(oracle.archived_checks(self.spec,self.files)),19)

    def test_unowned_control(self):
        self.change(['final/control-ay.jsonl'],lambda r:r['event']=='metadata',lambda r:r.update(owned=False))
        self.reject()

    def test_headless_branch_even_if_repeats_agree(self):
        self.change(['final/observe-ay.jsonl','final/repeat-ay.jsonl'],
                    lambda r:r.get('tick')==51 and r['event']=='cant-path-enter',
                    lambda r:r['unit'].update(head=[1,2]))
        self.reject()

    def test_positive_load_even_if_repeats_agree(self):
        self.change(['final/observe-bc-shore.jsonl','final/repeat-bc-shore.jsonl'],
                    lambda r:r['event']=='recover-load-leave',lambda r:r.update(result=0))
        self.reject()

    def test_shore_range_even_if_repeats_agree(self):
        self.change(['final/observe-bc-shore.jsonl','final/repeat-bc-shore.jsonl'],
                    lambda r:r['event']=='point-task-create' and r['code']==0xd016b,
                    lambda r:r.update(range=r['range']+1))
        self.reject()

    def test_additional_query_is_checked(self):
        self.change(['final/repeat-bc-shore.jsonl'],lambda r:r['event']=='shore-query',
                    lambda r:r.update(masks=[0]*7))
        self.reject()

    def test_arrival_release_even_if_repeats_agree(self):
        self.change(['map-c/observe4.jsonl','map-c/observe5.jsonl'],
                    lambda r:r.get('tick')==37 and r['event']=='arrival-leave',
                    lambda r:r.update(flags=2196))
        self.reject()

    def test_instrumented_control(self):
        self.change(['final/control-bc.jsonl'],lambda r:r['event']=='metadata',
                    lambda r:r.update(event='module'))
        self.reject()

    def test_archive_content_is_pinned(self):
        self.files['final/control-ay-preload.txt']+=' '
        self.reject()


if __name__ == '__main__':
    unittest.main()
