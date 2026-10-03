"""Reject partial file-backed evidence without retail assets or Frida."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('control', Path(__file__).resolve().parents[1] /
                                             'tools/frida/control_wc3_pathfinding.py')
control = importlib.util.module_from_spec(spec)
spec.loader.exec_module(control)


class ControlTests(unittest.TestCase):
    def samples(self):
        return [f'PATHTRACE tick={i} label=sample x=0 y=0 order=0' for i in range(1, 301)] + [
            'PATHTRACE tick=300 label=complete x=0 y=0 order=0']

    def test_preload_assets_excluded_markers_preserved(self):
        rows = self.samples()
        rows.insert(10, 'PATHTARGET tick=10 x=1 y=2')
        rows.insert(11, 'PATHCROWD tick=10 id=0 ordered=1')
        rows.insert(12, 'PATHWIDGET tick=0 x=-2160 y=-48')
        source = '\r\n'.join('\tcall Preload( "' + row + '" )' for row in ['units\\human.mdx'] + rows)
        self.assertEqual(control.markers(source), rows)
        control.validate(rows)

    def test_missing_duplicate_out_of_order_samples_rejected(self):
        rows = self.samples()
        for invalid in (rows[1:], rows[:10] + rows[9:], [rows[1], rows[0]] + rows[2:]):
            with self.assertRaises(ValueError):
                control.validate(invalid)

    def test_terminal_completion_required(self):
        with self.assertRaises(ValueError):
            control.validate(self.samples()[:-1])


if __name__ == '__main__':
    unittest.main()
