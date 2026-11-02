import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/wc3_pathfinding_benchmark.py'
spec = importlib.util.spec_from_file_location('wc3_benchmark', TOOL)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class BenchmarkContractTests(unittest.TestCase):
    def test_native_swap_capture_needs_no_screen_function_hook(self):
        prefix = r'''
#include <assert.h>
typedef struct { unsigned long long start[2]; } GumInvocationContext;
static void *gum_invocation_context_get_listener_invocation_data(GumInvocationContext *ctx, int size) {
    assert(size==16);return ctx->start;
}
#define PRESENTATION_CAPACITY 64
'''
        fixture = r'''
u32 phase[5];u64 rows[64*5],clocks[3];
static int finished;
static u64 wall=1000000,cpu=100000;
int gettime(int id,ts *t) { u64 value=id==3?cpu:wall;t->sec=value/1000000000;t->nsec=value%1000000000;return 0; }
void finish(void) { finished++; }
int main(void) {
    GumInvocationContext ctx={0};
    swap_enter(&ctx);swap_leave(&ctx);assert(phase[1]==0);
    phase[0]=1;wall+=16000000;swap_enter(&ctx);wall+=100;cpu+=50;swap_leave(&ctx);
    assert(phase[1]==1 && rows[1]==16000100 && rows[2]==50 && rows[3]==100 && rows[4]==1);
    phase[0]=2;
    for(int i=0;i<60;i++) {
        normal_enter(&ctx);wall+=16000000;swap_enter(&ctx);wall+=100;cpu+=50;swap_leave(&ctx);normal_leave(&ctx);
    }
    assert(phase[1]==61 && phase[2]==60 && phase[0]==3 && !phase[3] && finished==1);
    swap_enter(&ctx);swap_leave(&ctx);assert(phase[1]==61);
    phase[0]=2;phase[1]=PRESENTATION_CAPACITY;
    swap_enter(&ctx);swap_leave(&ctx);assert(phase[3]==1 && phase[1]==PRESENTATION_CAPACITY);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='wc3-swap-capture-') as folder:
            source = Path(folder) / 'capture.c'
            binary = Path(folder) / 'capture'
            source.write_text(prefix + benchmark.PRESENTATION_NATIVE + fixture)
            subprocess.run(['cc', '-std=c11', '-O2', str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_spawn_presentation_requires_post_batch_frames_and_rejects_one_gap(self):
        frames = [{'interval_ms': 16, 'phase': 1}]
        frames += [{'interval_ms': 16, 'phase': 2} for _ in range(60)]
        capture = {'frames': frames, 'post_host_frames': 60, 'overflow': False}
        self.assertTrue(benchmark.presentation_budget(capture)['passed'])
        frames[1]['interval_ms'] = 40
        budget = benchmark.presentation_budget(capture)
        self.assertFalse(budget['passed'])
        self.assertEqual(budget['double_period_gaps'], 1)
        self.assertEqual(budget['during_spawn_frames'], 1)
        self.assertFalse(benchmark.presentation_budget({'frames': frames[:2]})['capture_complete'])
        self.assertFalse(benchmark.presentation_budget({'frames': frames, 'post_host_frames': 1})['capture_complete'])
        capture['overflow'] = True
        self.assertFalse(benchmark.presentation_budget(capture)['capture_complete'])
        self.assertFalse(benchmark.presentation_budget({})['passed'])

    def test_display_budget_rejects_one_spike_even_with_cheap_average(self):
        rows = [{'event': 'host_frame', 'movement_cpu_ms': 0.01,
                 'movement_wall_ms': 0.01, 'setup': False} for _ in range(60)]
        rows[0].update(movement_cpu_ms=4.8, movement_wall_ms=4.8, setup=True)
        result = benchmark.display_budget(rows)
        self.assertEqual(result['allowance_ms'], 0.8)
        self.assertFalse(result['passed'])
        self.assertEqual(result['over_budget_frames'], 1)
        self.assertEqual(result['peak_ms'], 4.8)

    def test_display_budget_boundary_and_missing_owner_measurement(self):
        rows = [{'event': 'host_frame', 'movement_cpu_ms': 0.8,
                 'movement_wall_ms': 0.8, 'setup': False}]
        self.assertTrue(benchmark.display_budget(rows)['passed'])
        rows[0]['movement_cpu_ms'] = None
        self.assertFalse(benchmark.display_budget(rows)['passed'])
        self.assertFalse(benchmark.display_budget([])['passed'])

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
        for option, value in [('--units', '0'), ('--units', '4097'), ('--cohort', '13'),
                              ('--spacing', '0'), ('--distance', 'nan'), ('--duration', '-1'),
                              ('--reorder', '0'), ('--timeout', 'inf'), ('--cpu', '-1'),
                              ('--unit-types', 'earc,'), ('--unit-types', 'earc,hfo'),
                              ('--unit-types', 'earc,éarc'), ('--unit-types', ''),
                              ('--sample-cycles', '-1'), ('--sample-cycles', '2147483648'),
                              ('--path-scheduler', 'unknown')]:
            with self.subTest(option=option, value=value):
                result = subprocess.run([sys.executable, str(TOOL), '--data', '/unused',
                                         '--output', '/unused/report.jsonl', option, value],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn('Traceback', result.stderr)
                self.assertNotIn('Frida runtime', result.stderr)

    def test_keep_open_requires_a_rendered_run(self):
        result = subprocess.run([sys.executable, str(TOOL), '--data', '/unused',
                                 '--output', '/unused/report.jsonl', '--keep-open'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--keep-open requires --render', result.stderr)

    def test_missing_build_files_fail_before_loading_frida_or_inspecting_abi(self):
        with tempfile.TemporaryDirectory() as folder:
            base = Path(folder)
            binary, library = base / 'engine', base / 'libgame.so'
            for missing in ('--binary', '--library'):
                binary.write_bytes(b'engine'); library.write_bytes(b'game')
                (binary if missing == '--binary' else library).unlink()
                result = subprocess.run([sys.executable, str(TOOL), '--data', '/unused',
                                         '--binary', str(binary), '--library', str(library),
                                         '--output', str(base / 'report.jsonl')], capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn(missing + ' file not found:', result.stderr)
                self.assertNotIn('Traceback', result.stderr)
                self.assertNotIn('Frida runtime', result.stderr)
                self.assertNotIn('nm:', result.stderr)

    def test_keep_open_waits_for_user_close_and_timeout_still_cleans_up(self):
        # Exercise the real CLI lifecycle with an external-process adapter.
        # A completed capture stays attached beyond its timeout; an incomplete
        # capture still releases the session and terminates its own process.
        for captured, interrupted, interactive in ((True, False, False), (True, True, False),
                                                  (False, False, False), (True, False, True)):
            with self.subTest(captured=captured, interrupted=interrupted, interactive=interactive), tempfile.TemporaryDirectory() as folder:
                base = Path(folder)
                binary, library, report = base / 'engine', base / 'libgame.so', base / 'report.jsonl'
                binary.write_bytes(b'engine'); library.write_bytes(b'game')
                state = SimpleNamespace(detached=False, now=0, sleeps=0, kills=[], argv=None, source=None)
                listeners = {}

                def load():
                    if captured:
                        listeners['message']({'type': 'send', 'payload': {'event': 'final'}}, None)

                script = SimpleNamespace(on=lambda name, callback: listeners.update({name: callback}), load=load)

                class Session:
                    @property
                    def is_detached(self): return state.detached
                    def on(self, *args): pass
                    def create_script(self, source):
                        state.source = source
                        return script
                    def detach(self): state.detached = True

                def spawn(argv, **kwargs):
                    state.argv = argv
                    return 123

                def sleep(seconds):
                    state.now += seconds
                    state.sleeps += 1
                    if captured and state.sleeps == 5:
                        if interrupted: raise KeyboardInterrupt
                        state.detached = True

                device = SimpleNamespace(spawn=spawn, attach=lambda pid: Session(), resume=lambda pid: None,
                                         kill=lambda pid: state.kills.append(pid), on=lambda *args: None,
                                         off=lambda *args: None)
                frida = SimpleNamespace(get_local_device=lambda: device, ProcessNotFoundError=LookupError)
                argv = [str(TOOL), '--data', '/unused', '--binary', str(binary), '--library', str(library),
                        '--output', str(report), '--timeout', '0.2']
                argv += ['--interactive'] if interactive else ['--render', '--keep-open']
                with patch.dict(sys.modules, {'frida': frida}), patch.object(sys, 'argv', argv), \
                     patch.object(benchmark, 'layout', return_value={'pointer_size': 8, 'level_size': 8}), \
                     patch.object(benchmark, 'symbols', return_value={}), \
                     patch.object(benchmark.subprocess, 'check_output', return_value='00000000 00000008 B level\n'), \
                     patch.object(benchmark.time, 'monotonic', side_effect=lambda: state.now), \
                     patch.object(benchmark.time, 'sleep', side_effect=sleep):
                    result = benchmark.main()
                self.assertEqual(state.argv[state.argv.index('+com_frame_limit') + 1], '0')
                self.assertEqual(state.argv[state.argv.index('wc3_path_scheduler') + 1], 'responsive')
                rows = [json.loads(line) for line in report.read_text().splitlines()]
                config = rows[0]['metadata']
                self.assertEqual(config['interactive'], interactive)
                self.assertTrue(config['render'])
                self.assertTrue(config['keep_open'])
                self.assertEqual(config['spawn_only'], interactive)
                self.assertEqual(config['path_scheduler'], 'responsive')
                self.assertEqual(result, 0 if captured else 1)
                self.assertEqual(state.kills, [] if captured and not interrupted else [123])
                self.assertEqual(any(row.get('event') == 'kept_open' for row in rows), captured)
                if captured: self.assertGreater(state.now, 0.2)


if __name__ == '__main__':
    unittest.main()
