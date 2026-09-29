"""Asset-free differential checks of the production C arithmetic, plus trace rejection tests."""
import ctypes
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_pathing_numeric import add, subtract, multiply, bits, trig_bits, square_root, reciprocal
from generate_wc3_math_tables import sine_table, reciprocal_table
from verify_wc3_motion_trace import verify


class PathingMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='wc3-pathing-math-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.engines = []
        for opt in ('-O0', '-O2'):
            lib = Path(cls.temp.name) / (opt + '.so')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', opt, '-fPIC', '-shared',
                            '-I', str(ROOT), str(ROOT / 'tools/ghidra/wc3_pathing_engine_probe.c'), '-o', str(lib)], check=True)
            engine = ctypes.CDLL(str(lib))
            engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_integrate.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            for name in ('add', 'subtract', 'multiply', 'angle', 'sin', 'cos', 'sqrt', 'reciprocal'):
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32] * (2 if name in ('add', 'subtract', 'multiply') else 1)
                proc.restype = ctypes.c_uint32
            cls.engines.append(engine)

    def test_integer_reference_and_compiler_optimization_agree(self):
        rng = random.Random(0x12717085)
        pairs = [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(5000)]
        pairs += [(bits(1), bits(-1)), (bits(1), 0x33800000), (0, 0x80000000)]
        for name, reference in (('add', add), ('subtract', subtract), ('multiply', multiply)):
            for a, b in pairs:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(a, b), reference(a, b), (name, hex(a), hex(b)))

    def test_live_open_decision_and_window_boundary(self):
        # Frozen first open-ground Frida decision; the two nonzero significands truncate during addition.
        live = [1083572252, 1070141402, 0, 1083572224, 1058642330, 1065749137, 0]
        for engine in self.engines:
            words = (ctypes.c_uint32 * 7)(*live)
            engine.pathing_motion(words)
            self.assertEqual(list(words)[:2], [1091960846, 1070141402])
            for error, stopped in ((bits(0.5) - 1, False), (bits(0.5), True), (bits(0.5) + 1, True)):
                words = (ctypes.c_uint32 * 7)(bits(1), 0, error, bits(0.125), bits(0.125), bits(0.5), 0)
                engine.pathing_motion(words)
                self.assertEqual(words[0], 0 if stopped else bits(1.125))
                self.assertEqual(words[1], bits(0.125))

    def test_full_turn_and_signed_zero_retain_retail_quirks(self):
        for engine in self.engines:
            self.assertEqual(engine.pathing_angle(0xc0c90fdb), 0)
            self.assertEqual(engine.pathing_angle(0x40c90fdb), 0x40c90fdb)
            self.assertEqual(engine.pathing_angle(0), 0x80000000)

    def test_retail_trig_is_integer_interpolation(self):
        table = sine_table()
        rng = random.Random(0x71280)
        words = [bits(x) for x in (0, 0.125, 0.6, 1, 1.5707963267948966, -1.5707963267948966, 3.141592653589793)]
        words += [rng.getrandbits(32) for _ in range(5000)]
        for name, cosine in (('sin', False), ('cos', True)):
            for word in words:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(word), trig_bits(word, cosine, table),
                                     (name, hex(word)))

    def test_root_and_reciprocal_use_retail_significands(self):
        table = reciprocal_table()
        rng = random.Random(0x71570)
        words = [bits(x) for x in (0, -0.0, -1, 0.125, 0.6, 1, 2, 4096)]
        words += [rng.getrandbits(32) for _ in range(5000)]
        for name, model in (('sqrt', square_root), ('reciprocal', lambda w: reciprocal(w, table))):
            for word in words:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(word), model(word), (name, hex(word)))

    def test_complete_live_turn_velocity_and_integration(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['commits']), 192)
        for row in fixture['commits']:
            speed, heading, before, clock, after = row[0], row[1], row[2:10], row[10:13], row[13:21]
            for engine in self.engines:
                words = (ctypes.c_uint32 * 5)(*before[4:6], speed, heading, before[6])
                engine.pathing_velocity(words)
                self.assertEqual(list(words)[:2], after[4:6])
                self.assertEqual(list(words)[2:], [speed, heading, before[6]])
                words = (ctypes.c_uint32 * 11)(*before[2:6], *before[:2], *clock, 0, 0)
                engine.pathing_integrate(words)
                self.assertEqual(list(words)[:2], after[2:4])
                self.assertEqual(list(words)[4:6], after[:2])

    def test_trace_rejects_missing_truncated_and_mutated_decisions(self):
        rows = [dict(event='metadata', sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236', motionEvents=True),
                dict(event='motion-decision', mover='a', speed=bits(1), heading=0, error=0,
                     increment=bits(0.125), turn=bits(0.125), window=bits(0.5), stop=0,
                     nextSpeed=bits(1.125), nextHeading=0),
                dict(event='trace-end', installed=True, samples=0, counts={'motion-decision': 1})]
        self.assertEqual(verify(rows, self.engines[0], None)['exact_decisions'], 1)
        for key, value in (('nextSpeed', bits(1)), ('nextHeading', bits(0.125)), ('error', -1)):
            bad = [rows[0], {**rows[1], key: value}, rows[2]]
            with self.assertRaises(ValueError): verify(bad, self.engines[0], None)
        with self.assertRaises(ValueError): verify([rows[0], rows[2]], self.engines[0], None)
        rows[-1]['counts']['motion-decision'] = 2
        with self.assertRaises(ValueError): verify(rows, self.engines[0], None)

    def test_trace_rejects_missing_truncated_and_mutated_velocity(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        row = fixture['commits'][0]
        commit = dict(event='velocity-commit', mover='a', speed=row[0], heading=row[1],
                      before=row[2:10], clock=row[10:13], after=row[13:21])
        metadata = dict(event='metadata', sha256=fixture['binary_sha256'], motionEvents=True, velocityEvents=True)
        decision = dict(event='motion-decision', mover='a', speed=bits(1), heading=0, error=0,
                        increment=bits(0.125), turn=bits(0.125), window=bits(0.5), stop=0,
                        nextSpeed=bits(1.125), nextHeading=0)
        ending = dict(event='trace-end', installed=True, samples=0,
                      counts={'motion-decision': 1, 'velocity-commit': 1})
        self.assertEqual(verify([metadata, decision, commit, ending], self.engines[0], None)['exact_velocity_commits'], 1)
        with self.assertRaises(ValueError): verify([metadata, decision, ending], self.engines[0], None)
        bad_end = {**ending, 'counts': {**ending['counts'], 'velocity-commit': 2}}
        with self.assertRaises(ValueError): verify([metadata, decision, commit, bad_end], self.engines[0], None)
        for key, value in (('speed', -1), ('before', []), ('clock', [True, 0, 0])):
            with self.assertRaises(ValueError):
                verify([metadata, decision, {**commit, key: value}, ending], self.engines[0], None)
        for index in (0, 2, 4, 6):
            mutated = list(commit['after'])
            mutated[index] ^= 1
            with self.assertRaises(ValueError):
                verify([metadata, decision, {**commit, 'after': mutated}, ending], self.engines[0], None)


if __name__ == '__main__':
    unittest.main()
