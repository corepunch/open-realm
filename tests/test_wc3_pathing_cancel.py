"""Cancellation fixtures and strict acceptance reject weakened expectations."""
import copy
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from verify_wc3_pathing_cancel import FIXTURE,validate,engine_totals,words_digest
from cancel208_fixture import extract,render
from cancel208_queue_fixture import extract as queue_extract


class CancelTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(FIXTURE.read_text())
        self.turn=ROOT/'tools/ghidra/fixtures/retail-cancel208-turning-1.27.jsonl.gz'
        self.queue=ROOT/'tools/ghidra/fixtures/retail-cancel208-queue-1.27.jsonl.gz'

    def test_pins_and_original_generated_header(self):
        validate(self.spec)
        self.assertEqual(render(self.turn),(ROOT/'games/warcraft-3/game/tests/retail_cancel208.h').read_text())
        self.assertEqual(words_digest(extract(self.turn)),self.spec['scenes'][0]['words_sha256'])
        self.assertEqual(words_digest(queue_extract(self.queue)),self.spec['scenes'][1]['words_sha256'])

    def test_each_input_pin_is_required(self):
        for name in self.spec['pins']:
            bad=copy.deepcopy(self.spec);bad['pins'][name]='0'*64
            with self.assertRaises(ValueError):validate(bad)

    def test_counts_and_categories_cannot_be_weakened(self):
        for field in ('visual_count','turn_stops','queue_stops','searches','samples'):
            bad=copy.deepcopy(self.spec);bad[field]-=1
            with self.assertRaises(ValueError):validate(bad)
        bad=copy.deepcopy(self.spec);bad['engine_tests'].pop()
        with self.assertRaises(ValueError):validate(bad)
        bad=copy.deepcopy(self.spec);bad['scenes'][0]['preload_payload_limit']=1
        with self.assertRaises(ValueError):validate(bad)

    def test_empty_partial_failed_duplicate_engine_runs_fail(self):
        good='=== 14361/14361 assertions passed in 2 test(s) ==='
        self.assertEqual(engine_totals(good,0),14361)
        for text in ('',good+good,good.replace('2 test','1 test'),
                     good.replace('14361/14361','14360/14361'),'=== 0/0 assertions passed in 0 test(s) ==='):
            with self.assertRaises(ValueError):engine_totals(text,0)
        with self.assertRaises(ValueError):engine_totals(good,1)

    def corrupted(self,path,decoder,change):
        rows=[json.loads(line)for line in gzip.decompress(path.read_bytes()).decode().splitlines()]
        change(rows)
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'altered.jsonl';p.write_text(''.join(json.dumps(r)+'\n'for r in rows))
            with self.assertRaises(ValueError):decoder(p)

    def test_missing_visual_and_stop_returns_are_rejected(self):
        for event in ('visual-end','stop-end'):
            self.corrupted(self.turn,extract,lambda rows:rows.pop(next(i for i,r in enumerate(rows)if r['event']==event)))

    def test_missing_search_return_and_interleaved_stop_are_rejected(self):
        self.corrupted(self.queue,queue_extract,lambda rows:rows.pop(next(i for i,r in enumerate(rows)if r['event']=='search-end')))
        def change(rows):
            next(r for r in rows if r['event']=='stop-begin')['searchDepth']=1
        self.corrupted(self.queue,queue_extract,change)

    def test_changed_survivor_fifo_is_rejected(self):
        def change(rows):
            r=next(r for r in rows if r['event']=='marker' and 'label=pending_stopped ' in r['value'])
            q=r['buckets'][3]['queue'];q[1],q[2]=q[2],q[1]
        self.corrupted(self.queue,queue_extract,change)


if __name__=='__main__':unittest.main()
