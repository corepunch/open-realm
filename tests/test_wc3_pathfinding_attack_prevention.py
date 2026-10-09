"""Frozen native outputs and strict public capture admission."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_attack_prevention155_live import HASH, read_capture


class AttackPreventionEvidence(unittest.TestCase):
    def capture(self, directory):
        markers = ['RSG tick=%d label=sample' % i for i in range(99)] + ['RSG tick=280 label=complete']
        path = directory / 'observe.jsonl'
        preload = path.with_name('observe-preload.txt')
        preload.write_text('\n'.join('call Preload( "%s" )' % m for m in markers))
        rows = [dict(event='metadata', mode='observe', sha256=HASH,
                     task='captain-prevention155', source_sha256={'map': 'map-pin'})]
        rows += [dict(event='marker', value=m) for m in markers]
        rows += [dict(event='preload-file', complete=True,
                      sha256=hashlib.sha256(preload.read_bytes()).hexdigest()),
                 dict(event='trace-end', installed=True,
                      counts=dict(apply=10, suppression=16, remove=9, spells=3))]
        path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
        expected = dict(map_sha256='map-pin', captures={path.name: hashlib.sha256(path.read_bytes()).hexdigest()})
        return path, expected, rows

    def test_completed_capture_is_admitted(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, _ = self.capture(Path(directory))
            _, markers = read_capture(path, 'observe', expected)
            self.assertEqual(len(markers), 100)

    def test_missing_observer_events_are_rejected_even_with_updated_file_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, rows = self.capture(Path(directory))
            rows[-1]['counts']['suppression'] = 15
            path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
            expected['captures'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.assertRaisesRegex(ValueError, 'incomplete observer'):
                read_capture(path, 'observe', expected)

    def test_duplicate_metadata_and_capture_errors_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, rows = self.capture(Path(directory))
            for extra in (copy.deepcopy(rows[0]), dict(type='error', description='observer fault')):
                path.write_text('\n'.join(json.dumps(r) for r in rows + [extra]) + '\n')
                with self.assertRaises(ValueError):
                    read_capture(path, 'observe', expected)

    def test_zero_exit_without_public_completion_is_not_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, _ = self.capture(Path(directory))
            preload = path.with_name('observe-preload.txt')
            preload.write_text('call Preload( "RSG tick=0 label=start" )')
            with self.assertRaises(ValueError):
                read_capture(path, 'observe', expected)

    def test_engine_tables_are_the_frozen_native_outputs(self):
        data = json.loads((ROOT / 'tools/ghidra/fixtures/retail-attack-prevention-native-1.27.json').read_text())
        header = (ROOT / 'games/warcraft-3/game/tests/retail_attack_prevention.h').read_text()
        self.assertEqual(data['complete_slot_calls'], 2759)
        self.assertEqual(data['complete_counter_calls'], 80)
        for row in data['slots']:
            self.assertIn('{%u,%u,{%s}}' % (row['weapon'], row['targets'], ','.join(map(str, row['enabled']))), header)
        for row in data['counters']:
            self.assertIn('{0x%08xu,%u,%u,{%s}}' % (row['initial'] & 0xffffffff, row['release'], row['mask'],
                ','.join('0x%08xu' % word for word in row['words'])), header)

    def test_spell_and_attack_counters_have_separate_saved_native_owners(self):
        schema = json.loads((ROOT / 'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        layouts = {row['name']: row for row in schema['layouts']}
        attack = {f['offset']: f for f in layouts['WC3AttackRangePrefix']['fields']}
        unit = {f['offset']: f for f in layouts['WC3UnitOrdersPrefix']['fields']}
        for offset in (0x224, 0x228, 0x22c):
            self.assertEqual(attack[offset]['type'], 'i32')
        self.assertEqual(unit[0x1d0]['name'], 'spell_prevention_count')
        method = next(row for row in schema['methods'] if row['address'] == '6f48f380')
        self.assertEqual([p['storage'] for p in method['parameters']], ['ECX', 4])


if __name__ == '__main__':
    unittest.main()
