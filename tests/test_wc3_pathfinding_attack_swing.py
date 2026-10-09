"""Compare the production scalar helper to original committed-hit witnesses."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class AttackSwingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows=json.loads((ROOT/'tools/ghidra/fixtures/research/attack174-swing.json').read_text())['rows']
        cls.temp=tempfile.TemporaryDirectory()
        source=Path(cls.temp.name)/'probe.c';library=Path(cls.temp.name)/'probe.so'
        source.write_text('#include "games/warcraft-3/common/wc3_pathing_speed.h"\n'
            'uint32_t probe(uint32_t b,uint32_t d,uint32_t r,uint32_t *out) {'
            'float remaining=wc3_float(r); float delay=wc3_attack_swing_delay(wc3_float(b),wc3_float(d),&remaining);'
            '*out=wc3_float_bits(remaining);return wc3_float_bits(delay);}\n')
        subprocess.run(['cc','-O2','-shared','-fPIC','-I',str(ROOT),str(source),'-o',str(library)],check=True,capture_output=True)
        cls.lib=ctypes.CDLL(str(library));cls.lib.probe.argtypes=[ctypes.c_uint32]*3+[ctypes.POINTER(ctypes.c_uint32)]
        cls.lib.probe.restype=ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def test_all_original_swing_words(self):
        self.assertEqual(len(self.rows),288)
        for row in self.rows:
            cooldown=ctypes.c_uint32()
            result=self.lib.probe(row['backswing'],row['divisor'],row['remaining'],ctypes.byref(cooldown))
            self.assertEqual(result,row['requests'][-1][2],row)
            self.assertEqual(cooldown.value,row['requests'][0][2] if len(row['requests'])==2 else row['remaining'],row)

    def test_independent_timer_and_cooldown_requests(self):
        self.assertEqual({r['slot'] for r in self.rows},{0,1})
        self.assertEqual({len(r['requests']) for r in self.rows},{1,2})
        for row in self.rows:
            self.assertEqual(row['requests'][-1][:2],[0x200,0xd01b2])
            if len(row['requests'])==2:self.assertEqual(row['requests'][0],[0x1d8,0xd01b0,0x3ca3d70a])


if __name__=='__main__':unittest.main()
