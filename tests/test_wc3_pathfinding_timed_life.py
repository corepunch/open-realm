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
from research.verify_timed_life156_live import HASH, read_capture


class TimedLifeEvidence(unittest.TestCase):
    def capture(self, directory):
        markers = ['RSG tick=%d label=sample' % i for i in range(1120)] + ['RSG tick=60 label=complete']
        path = directory / 'observe.jsonl'
        preload = path.with_name('observe-preload.txt')
        preload.write_text('\n'.join('call Preload( "%s" )' % m for m in markers))
        rows = [dict(event='metadata', mode='observe', sha256=HASH,
                     task='timed-life156', source_sha256={'map': 'map-pin'})]
        rows += [dict(event=event) for event in ('native','factory','initialize') for _ in range(20)]
        rows += [dict(event='marker', value=m) for m in markers]
        rows += [dict(event='preload-file', complete=True,
                      sha256=hashlib.sha256(preload.read_bytes()).hexdigest()),
                 dict(event='trace-end', installed=True,
                      counts=dict(native=20, factory=20, initialize=20))]
        path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
        expected = dict(map_sha256='map-pin', captures={path.name: hashlib.sha256(path.read_bytes()).hexdigest()})
        return path, expected, rows

    def test_completed_capture_is_admitted(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, _ = self.capture(Path(directory))
            _, markers = read_capture(path, 'observe', expected)
            self.assertEqual(len(markers), 1121)

    def test_missing_observer_events_are_rejected_even_with_updated_file_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            path, expected, rows = self.capture(Path(directory))
            rows[-1]['counts']['initialize'] = 19
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

    def test_engine_marker_table_is_the_full_frozen_public_timeline(self):
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-timed-life-live-1.27.json').read_text())
        header=(ROOT/'games/warcraft-3/game/tests/retail_timed_life_156.h').read_text()
        import re
        markers=[json.loads(line.strip().rstrip(',')) for line in
                 header.split('static char const timedlife_script_156')[0].splitlines()
                 if re.match(r'\s*"RSG ',line)]
        self.assertEqual(markers,data['public_markers'])
        self.assertEqual(len(markers),1121)

if __name__=='__main__':unittest.main()
