import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/wc3_pathfinding_benchmark.py'
spec = importlib.util.spec_from_file_location('wc3_benchmark', TOOL)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class BenchmarkContractTests(unittest.TestCase):
    def test_compiled_abi_contains_complete_native_records(self):
        abi = benchmark.layout(ROOT)
        self.assertEqual(abi['pointer_size'], 8)
        self.assertLessEqual(abi['point'] + abi['pointer_size'], abi['request_size'])
        self.assertGreaterEqual(abi['request_count'], abi['max_group'] * abi['member_size'])
        self.assertLessEqual(abi['member_spawn'] + 4, abi['member_size'])
        self.assertNotEqual(abi['geometry_from'], abi['geometry_target'])
        for field, size in [('s_origin2', 8), ('movement_velocity', 8), ('spawn_time', 4)]:
            self.assertLessEqual(abi[field] + size, abi['edict_size'])

    def test_invalid_workloads_fail_before_attaching_to_a_process(self):
        for option, value in [('--units', '0'), ('--units', '2049'), ('--cohort', '13'),
                              ('--spacing', '0'), ('--distance', 'nan'), ('--duration', '-1'),
                              ('--reorder', '0'), ('--timeout', 'inf')]:
            with self.subTest(option=option, value=value):
                result = subprocess.run([sys.executable, str(TOOL), '--data', '/unused',
                                         '--output', '/unused/report.jsonl', option, value],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn('Traceback', result.stderr)
                self.assertNotIn('Frida runtime', result.stderr)


if __name__ == '__main__':
    unittest.main()
