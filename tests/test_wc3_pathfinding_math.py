"""Asset-free differential checks of the production C arithmetic, plus trace rejection tests."""
import ctypes
import copy
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_pathing_numeric import add, subtract, multiply, bits, trig_bits, square_root, reciprocal, acos_bits, fractional, modulo, decimal_bits, integer_float, saturating_integer_word, asin_bits, atan_bits, atan2_bits, divide, floor_word, ceil_word, round_word, truncate_word
from generate_wc3_math_tables import sine_table, reciprocal_table, acos_tables
from verify_wc3_motion_trace import verify
from verify_wc3_pathing_integers import integer_literal_word, source_word as integer_source_word
from verify_wc3_pathing_literals import literal_word, source_word
from verify_wc3_pathing_power import corelog, reducedlog, log, exp, power, public as public_power
from verify_wc3_byte_inputs import source_bytes
from verify_wc3_heading_aliases import verify as verify_heading_aliases
from verify_wc3_arrival_trace import verify as verify_arrival, configure as configure_arrival


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
            engine.pathing_velocity_world_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity_heading.argtypes = [ctypes.c_uint32]*3
            engine.pathing_velocity_heading.restype = ctypes.c_uint32
            engine.pathing_velocity.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_integrate.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_heading_error.argtypes = [ctypes.c_uint32]*3
            engine.pathing_heading_error.restype = ctypes.c_uint32
            for name in ('add', 'subtract', 'multiply', 'modulo', 'fractional', 'facing_angle', 'angle', 'sin', 'cos', 'acos', 'sqrt', 'reciprocal'):
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32] * (2 if name in ('add', 'subtract', 'multiply','modulo') else 1)
                proc.restype = ctypes.c_uint32
            cls.engines.append(engine)

    def test_arrival_matches_complete_original_predicate_words(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-arrival-predicate-1.27.json').read_text())
        for engine in self.engines:
            engine.pathing_arrival.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 7)(*case['input'])
                    output = (ctypes.c_uint32 * 4)()
                    engine.pathing_arrival(inputs, output)
                    self.assertEqual(list(output), case['output'], case['input'])
                    self.assertEqual(list(inputs), case['input'])

    def test_point_arrival_observer_rejects_missing_or_changed_contract(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-point-arrival-inputs-1.27.json').read_text())
        rows = [dict(event='metadata', sha256=fixture['binary_sha256'], owned=True,
                     source_sha256=fixture['source_sha256'], motionEvents=True, velocityEvents=True, taskEvents=True),
                fixture['input'], fixture['publication'], *fixture['sample'],
                dict(event='trace-end', installed=True, counts={'arrival-input': 1, 'arrival-range': 1,
                     'arrival-evaluation': 2, 'velocity-commit': 2})]
        mutations = []
        changed = copy.deepcopy(rows); changed.pop(3); mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1]['worldRange'] = 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['after'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3]['storedPosition'][0] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3]['flags'] = 0x10000; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3], changed[4] = changed[4], changed[3]; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[5]['angle'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[6]['after'][4] = 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[0]['source_sha256']['map'] = '0' * 64; mutations.append(changed)
        changed = copy.deepcopy(rows); changed.insert(1, dict(type='error')); mutations.append(changed)
        changed = copy.deepcopy(rows); changed.pop(); mutations.append(changed)
        for engine in self.engines:
            configure_arrival(engine)
            # Motion replay is independently covered; isolate the added producer/pairing checks.
            with patch('verify_wc3_arrival_trace.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_arrival(rows, engine, fixture)['arrival_evaluations'], 2)
                for changed in mutations:
                    with self.assertRaises(ValueError):
                        verify_arrival(changed, engine, fixture)

    def power_bridge(self, engine, name, a, b=0):
        proc = getattr(engine, 'pathing_' + name)
        proc.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        proc.restype = None
        result = (ctypes.c_uint32 * 2)(0xabcdef01, 0xabcdef02)
        proc(a, b, result)
        return list(result)

    def test_heading_alias_observer_rejects_changed_or_missing_producer_evidence(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-heading-aliases-1.27.json').read_text())
        fixture['heading_cases'] = 1
        fixture['alias_sha256'] = fixture['sample_sha256']
        rows = [dict(event='metadata', source_sha256=fixture['source_sha256'],
                     sha256=fixture['target_sha256'], headingEvents=True), *fixture['sample'],
                dict(event='trace-end', counts={'heading-acos-alias': 1, 'heading-vector-alias': 1, 'heading-error': 1})]
        mutations = []
        changed = copy.deepcopy(rows); changed.pop(1); mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1], changed[2] = changed[2], changed[1]; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1]['inputPointer'] = changed[1]['outputPointer']; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['length'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['sequence'] += 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[0]['source_sha256']['map'] = '0' * 64; mutations.append(changed)
        for engine in self.engines:
            # Motion's existing tests cover its complete trace contract; isolate the added nested observer checks.
            with patch('verify_wc3_heading_aliases.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_heading_aliases(rows, engine, fixture)['alias_cases'], 1)
                for changed in mutations:
                    with self.assertRaises(ValueError):
                        verify_heading_aliases(changed, engine, fixture)

    def test_compiled_integers_match_wrapped_model_and_live_native_inputs(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-compiled-integer-inputs-1.27.json').read_text())
        rng = random.Random(0x925350)
        texts = [c['input'].lstrip('-') for c in fixture['cases']]
        for _ in range(2000):
            word = rng.getrandbits(260)
            texts.extend([str(word), '0' + format(word, 'o'), '$' + format(word, 'x'), '0x' + format(word, 'X')])
        for engine in self.engines:
            engine.pathing_integer_literal.argtypes = [ctypes.c_char_p]
            engine.pathing_integer_literal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_integer_literal(text.encode()), integer_literal_word(text), text)
        for case in fixture['cases']:
            self.assertEqual(integer_source_word(case['input']), case['input_word'])
            self.assertEqual(integer_float(case['input_word']), case['output'])

    def test_compiled_literals_match_live_inputs_and_independent_model(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-compiled-literal-inputs-1.27.json').read_text())
        rng = random.Random(0x925260)
        texts = [c['input'].lstrip('-') for c in fixture['cases']]
        texts += [str(rng.getrandbits(180)) + '.' + str(rng.getrandbits(240)) for _ in range(2000)]
        for engine in self.engines:
            engine.pathing_literal.argtypes = [ctypes.c_char_p]
            engine.pathing_literal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_literal(text.encode()), literal_word(text), text)
        for case in fixture['cases']:
            self.assertEqual(source_word(case['input']), case['input_word'])
            self.assertEqual(saturating_integer_word(case['input_word']), case['output'])
        self.assertNotEqual(literal_word('0.59999999999999998'), decimal_bits('0.59999999999999998', reciprocal_table()))

    def test_power_curves_and_compiler_optimization_agree(self):
        rng = random.Random(0x127190)
        words = [rng.getrandbits(32) for _ in range(3000)]
        words += [bits(v / 32) for v in range(-256, 257)]
        for engine in self.engines:
            for name, model in [('corelog', corelog), ('reducedlog', reducedlog), ('log', log), ('exp', exp)]:
                for word in words:
                    try:
                        expected = [1, model(word)]
                    except ValueError:
                        expected = [0, 0xabcdef02]
                    self.assertEqual(self.power_bridge(engine, name, word), expected, (name, hex(word)))

    def test_public_power_matches_live_words(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-power-inputs-1.27.json').read_text())
        for case in fixture['cases']:
            a, b = case['input_word']
            self.assertEqual(public_power(a, b), case['output'])
            for engine in self.engines:
                self.assertEqual(self.power_bridge(engine, 'public_power', a, b), [1, case['output']])

    def test_power_nontermination_does_not_invent_output(self):
        for engine in self.engines:
            for name, a, b in [('power', bits(2), bits(2147483648)),
                               ('public_power', bits(2), bits(2147483648)),
                               ('exp', bits(536870912), 0)]:
                self.assertEqual(self.power_bridge(engine, name, a, b), [0, 0xabcdef02])
            self.assertEqual(self.power_bridge(engine, 'power', bits(2), bits(3)), [1, bits(8)])

    def test_rounding_boundaries_and_raw_words_match_independent_models(self):
        rng = random.Random(0x70110)
        words = [rng.getrandbits(32) for _ in range(5000)] + [0, 0x80000000, 1, 0x80000001]
        for value in (-8388608, -2, -1.5, -1, -.5, 0, .5, 1, 1.5, 2, 8388608):
            words.extend((bits(value) + step) & 0xffffffff for step in range(-4, 5))
        for engine in self.engines:
            for name, model in [('floor', floor_word), ('ceil', ceil_word), ('round', round_word),
                                ('truncate', truncate_word)]:
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word), model(word), (name, hex(word)))
            self.assertEqual(engine.pathing_ceil(0), bits(1))
            self.assertEqual(engine.pathing_ceil(0x80000000), bits(1))
            self.assertEqual(engine.pathing_floor(0x80000000), 0)
            self.assertEqual(engine.pathing_round(bits(-.5)), 0)

    def test_integer_reference_and_compiler_optimization_agree(self):
        rng = random.Random(0x12717085)
        pairs = [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(5000)]
        pairs += [(bits(1), bits(-1)), (bits(1), 0x33800000), (0, 0x80000000)]
        for name, reference in (('add', add), ('subtract', subtract), ('multiply', multiply)):
            for a, b in pairs:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(a, b), reference(a, b), (name, hex(a), hex(b)))

    def test_paired_trig_matches_independent_raw_word_model(self):
        rng = random.Random(0x71340)
        angles = [rng.getrandbits(32) for _ in range(20000)]
        for angle in (0, 0x80000000, 1, 0x80000001, 0x3f490fdb, 0x3fc90fdb, 0x40490fdb, 0x40c90fdb):
            angles.extend((angle + offset) & 0xffffffff for offset in range(-2, 3))
        sines = sine_table()
        for engine in self.engines:
            proc = engine.pathing_sincos
            proc.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
            proc.restype = None
            for angle in angles:
                result = (ctypes.c_uint32 * 2)()
                proc(angle, result)
                self.assertEqual(list(result), [trig_bits(angle, False, sines), trig_bits(angle, True, sines)])

    def test_remaining_angle_helpers_match_integer_models_and_optimization(self):
        table = reciprocal_table()
        ordinary, near = acos_tables()
        sines = sine_table()
        rng = random.Random(0x705b0)
        words = [rng.getrandbits(32) for _ in range(20000)]
        words += [w | sign for sign in (0,0x80000000) for pivot in (0x3e8930a3,0x3f7e8000,0x3f800000)
                  for w in range(pivot-16,pivot+17)]
        models = [('asin',lambda w:asin_bits(w,ordinary,near)), ('atan',lambda w:atan_bits(w,table)),
                  ('tan',lambda w:divide(trig_bits(w,False,sines),trig_bits(w,True,sines),table)),
                  ('degrees_to_radians',lambda w:multiply(w,0x3c8efa35)),
                  ('radians_to_degrees',lambda w:multiply(w,0x42652ee1))]
        for engine in self.engines:
            for name, model in models:
                proc = getattr(engine,'pathing_'+name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word),model(word),(name,hex(word)))
            engine.pathing_atan2.argtypes = [ctypes.c_uint32,ctypes.c_uint32]
            engine.pathing_atan2.restype = ctypes.c_uint32
            for y,x in zip(words,words[1:]):
                self.assertEqual(engine.pathing_atan2(y,x),atan2_bits(y,x,table),(hex(y),hex(x)))

    def test_decimal_and_public_integer_conversion_match_independent_models(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-numeric-inputs-1.27.json').read_text())
        texts = [case['input'] for case in fixture['cases'] if case['native'] == 'S2R']
        rng = random.Random(0x70de0)
        for _ in range(2000):
            digits = ''.join(str(rng.randrange(10)) for _ in range(rng.randrange(1, 100)))
            pivot = rng.randrange(len(digits) + 1)
            texts.append(rng.choice(['', '+', '-']) + digits[:pivot] + '.' + digits[pivot:] + rng.choice(['', 'e2', ';tail', ' tail', '..7']))
        words = [rng.getrandbits(32) for _ in range(20000)]
        table = reciprocal_table()
        for engine in self.engines:
            engine.pathing_decimal.argtypes = [ctypes.c_char_p]
            engine.pathing_decimal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_decimal(text.encode()), decimal_bits(text, table), text)
            for name, model in [('integer_float', integer_float), ('saturating_integer', saturating_integer_word)]:
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word), model(word), (name, hex(word)))
            for case in fixture['cases']:
                if case['native'] == 'S2R':
                    self.assertEqual(engine.pathing_decimal(case['input'].encode()), case['output'], case['id'])

    def test_decimal_bytes_match_live_public_words_and_all_byte_domains(self):
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-public-byte-inputs-1.27.json').read_text())
        table = reciprocal_table()
        for engine in self.engines:
            engine.pathing_decimal.argtypes = [ctypes.c_char_p]
            engine.pathing_decimal.restype = ctypes.c_uint32
            for case in fixture['cases']:
                source = source_bytes(case)
                self.assertEqual(engine.pathing_decimal(source), case['output'], case['id'])
                self.assertEqual(decimal_bits(source.decode('latin1'), table), case['output'], case['id'])
            for byte in range(1, 256):
                for source in (bytes([byte]), b'12'+bytes([byte])+b'34', b'.5'+bytes([byte])+b'7', b'-'+bytes([byte])+b'0.2'):
                    self.assertEqual(engine.pathing_decimal(source), decimal_bits(source.decode('latin1'), table), source.hex())

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

    def test_fraction_remainder_and_committed_facing(self):
        rng=random.Random(0x70fe0)
        table=reciprocal_table()
        pairs=[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(5000)]
        for engine in self.engines:
            for a,b in pairs:
                self.assertEqual(engine.pathing_fractional(a),fractional(a))
                self.assertEqual(engine.pathing_modulo(a,b),modulo(a,b,table),(hex(a),hex(b)))
            self.assertEqual(engine.pathing_facing_angle(0),0)
            self.assertEqual(engine.pathing_facing_angle(0x80000000),0x80000000)
            self.assertEqual(engine.pathing_facing_angle(0x40c90fdb),0x35490fdb)
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-committed-facing-1.27.json').read_text())
        for heading,expected in fixture['angles']:
            for engine in self.engines:self.assertEqual(engine.pathing_facing_angle(heading),expected)
        for x,y,maximum,before,after in fixture['cases']:
            for engine in self.engines:
                self.assertEqual(engine.pathing_velocity_heading(x,y,before),after)

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

    def test_inverse_trig_thresholds_and_heading_fixture(self):
        ordinary,near = acos_tables()
        rng = random.Random(0x6ffa0)
        words = [rng.getrandbits(32) for _ in range(5000)]
        words += [w | sign for sign in (0,0x80000000)
                  for pivot in (0x3f7e8000,0x3f800000) for w in range(pivot-16,pivot+17)]
        for word in words:
            for engine in self.engines:
                self.assertEqual(engine.pathing_acos(word), acos_bits(word,ordinary,near), hex(word))
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-heading-errors-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),441)
        for x,y,heading,error in fixture['cases']:
            for engine in self.engines:
                self.assertEqual(engine.pathing_heading_error(x,y,heading),error,(hex(x),hex(y),hex(heading)))

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
                full = (ctypes.c_uint32 * 6)(*before[4:6],speed,heading,*before[6:8])
                engine.pathing_velocity_commit(full)
                self.assertEqual(list(full)[:2],after[4:6])
                self.assertEqual(full[5],after[7])
                words = (ctypes.c_uint32 * 11)(*before[2:6], *before[:2], *clock, 0, 0)
                engine.pathing_integrate(words)
                self.assertEqual(list(words)[:2], after[2:4])
                self.assertEqual(list(words)[4:6], after[:2])

    def test_world_velocity_adapter_matches_original_cutoffs_and_live_words(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-world-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),1040)
        self.assertTrue(any(c['input'][2]>0 and c['expected'][:2]==[0,0] for c in fixture['cases']))
        for row in fixture['cases']:
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*row['input'])
                engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],row['expected'],row['input'])
                self.assertEqual(list(words)[2:5],row['input'][2:5])
        live=json.loads((ROOT/'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        for row in live['commits']:
            speed,heading,before,after=row[0],row[1],row[2:10],row[13:21]
            inputs=[multiply(w,0x42000000) for w in before[4:6]]+[multiply(speed,0x42000000),heading,multiply(before[6],0x42000000),before[7]]
            expected=[multiply(w,0x42000000) for w in after[4:6]]+[after[7]]
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*inputs);engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],expected)

    def test_shared_wall_pair_owner_commits_match_production_world_adapter(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-wall-pair-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),46)
        self.assertEqual({r['role'] for r in fixture['cases']},{'first','second'})
        for row in fixture['cases']:
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*row['world_input'])
                engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],row['expected'])

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
        for index in (0, 2, 4, 6, 7):
            mutated = list(commit['after'])
            mutated[index] ^= 1
            with self.assertRaises(ValueError):
                verify([metadata, decision, {**commit, 'after': mutated}, ending], self.engines[0], None)

    def test_heading_capture_rejects_incomplete_or_changed_results(self):
        x,y,current,error = json.loads((ROOT/'tools/ghidra/fixtures/retail-heading-errors-1.27.json').read_text())['cases'][27]
        metadata = dict(event='metadata',sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',motionEvents=True,headingEvents=True)
        decision = dict(event='motion-decision',mover='a',speed=bits(1),heading=0,error=0,
                        increment=0,turn=bits(.5),window=bits(.5),stop=0,nextSpeed=bits(1),nextHeading=0)
        heading = dict(event='heading-error',vector=[x,y],heading=current,error=error)
        ending = dict(event='trace-end',installed=True,samples=0,counts={'motion-decision':1,'heading-error':1})
        self.assertEqual(verify([metadata,decision,heading,ending],self.engines[0],None)['exact_heading_errors'],1)
        with self.assertRaises(ValueError):verify([metadata,decision,ending],self.engines[0],None)
        for changed in ({'vector':[]},{'heading':True},{'error':error^1}):
            with self.assertRaises(ValueError):verify([metadata,decision,{**heading,**changed},ending],self.engines[0],None)
        with self.assertRaises(ValueError):verify([metadata,decision,heading,{**ending,'counts':{'motion-decision':1,'heading-error':2}}],self.engines[0],None)

if __name__ == '__main__':
    unittest.main()
