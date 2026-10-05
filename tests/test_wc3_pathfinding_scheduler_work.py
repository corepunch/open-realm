"""Preserve original-code work-boundary evidence and generated engine inputs."""
import gzip
import hashlib
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]


class SchedulerWorkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.report=json.loads(gzip.decompress((cls.fixture/'retail-scheduler-work-1.27.json.gz').read_bytes()))

    def test_original_code_generation_and_engine_header(self):
        report=self.report
        self.assertTrue(report['passed'])
        self.assertEqual(report['binary_sha256'],'d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236')
        self.assertEqual(report['work_charge_slices'],[[0x6f166fcd,0x6f166fdd],[0x6f166db3,0x6f166dc5]])
        source=gzip.decompress((self.fixture/'sources'/(report['oracle_sha256']+'.gz')).read_bytes())
        self.assertEqual(hashlib.sha256(source).hexdigest(),report['oracle_sha256'])
        header=(ROOT/'games/warcraft-3/game/tests/retail_scheduler_work.h').read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(),report['header_sha256'])
        expected=['/* Generated from original x86 post-search charge slices; boundary states are synthetic. */',
                  '/* SHA256 '+report['binary_sha256']+' */','static uint32_t const retail_scheduler_work[][6]={']
        expected+=['    {'+','.join(str(v)+'u' for v in row[1:])+'},'
                   for row in report['work_charge_rows'] if row[0]==0]
        self.assertEqual(header,('\n'.join(expected+['};',''])).encode())

    def test_all_classes_and_policies_include_actual_unsigned_wrap(self):
        rows=self.report['work_charge_rows']
        self.assertEqual(len(rows),2560)
        self.assertEqual(self.report['work_charge_cases'],2560)
        self.assertEqual(len({tuple(r[:4]) for r in rows}),2560)
        for player in range(16):
            for kind in range(4):
                matching=[r for r in rows if r[:4]==[player,kind,0xffffffff,1]]
                self.assertEqual(matching,[[player,kind,0xffffffff,1,0,0,1]])
                matching=[r for r in rows if r[:4]==[player,kind,0xffffffe0,64]]
                # Fine clears from the wrapped cumulative value; coarse keeps
                # its time because this search charged64, despite wrap to32.
                self.assertEqual(matching,[[player,kind,0xffffffe0,64,32,0 if kind==3 else 1037,1]])


if __name__=='__main__':unittest.main()
