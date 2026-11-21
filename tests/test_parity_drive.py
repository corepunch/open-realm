"""Command-contract tests for tools/parity/wc3_drive.sh; no display, gamescope, xdotool or game data required.

Fake gamescope/gamescopectl/xdotool executables on PATH record their arguments. The fake xdotool answers a typed
console command by appending the lines of a per-test reply script to the driver's game log, as the game would.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVE = ROOT / 'tools/parity/wc3_drive.sh'
FAKES = {
    'gamescope': 'while [ "$1" != -- ]; do shift; done; shift\n'
                 'DISPLAY=:99 GAMESCOPE_WAYLAND_DISPLAY=gamescope-99 exec "$@"\n',
    'gamescopectl': '[ "$1" = screenshot ] && printf PNG > "$2"\n',
    'xdotool': 'echo "$*" >> "$FAKE/xdotool.log"\n'
               'if [ "$1" = type ]; then "$FAKE/reply" "${@: -1}" >> "$WC3_DRIVE_STATE/game.log"; fi\n',
    'game': 'printf "%s\\n" "$@" > "$FAKE/args"; env > "$FAKE/env"\n'
            'echo "OpenWarcraft3 initialized." >&2; exec sleep 30\n',
}
FRAME = 'UIFRAME id=3 name={} type=GLUETEXTBUTTON fdf=- parent=- visible={} enabled={} interactive=1 hit={} text="" tex=-'


class ParityDriveTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.fake = Path(self.dir.name)
        self.state = self.fake / 'state'
        for name, body in FAKES.items():
            self.write(name, body)
        self.reply('echo')
        self.env = dict(os.environ, PATH=f'{self.fake}:{os.environ["PATH"]}', FAKE=str(self.fake),
                        WC3_DRIVE_STATE=str(self.state), WC3_BINARY=str(self.fake / 'game'), WC3DATA=str(self.fake))

    def tearDown(self):
        self.drive('stop')
        self.dir.cleanup()

    def write(self, name, body):
        path = self.fake / name
        path.write_text('#!/bin/bash\n' + body)
        path.chmod(0o755)

    def reply(self, body):
        self.write('reply', body + '\n')

    def attach(self):
        self.state.mkdir(exist_ok=True)
        (self.state / 'env').write_text('DISPLAY=:99\nGAMESCOPE_WAYLAND_DISPLAY=gamescope-99\n')
        (self.state / 'game.log').touch()

    def drive(self, *args):
        return subprocess.run(['bash', str(DRIVE), *args], env=self.env, text=True, capture_output=True, timeout=60)

    def xdotool(self):
        path = self.fake / 'xdotool.log'
        return path.read_text().splitlines() if path.exists() else []

    def test_start_passes_data_edition_and_ui_test_gate(self):
        self.env['XKB_DEFAULT_LAYOUT'] = 'de'
        result = self.drive('start', '-roc')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = (self.fake / 'args').read_text().splitlines()
        self.assertEqual(args[:3], ['-data', str(self.fake), '-roc'])
        env = (self.fake / 'env').read_text().splitlines()
        self.assertIn('OPENREALM_UI_TEST=1', env)
        self.assertIn(f'XDG_DATA_HOME={self.state}/data', env)
        # The console key is the US grave key whatever the desktop layout is.
        self.assertIn('XKB_DEFAULT_LAYOUT=us', env)
        self.assertEqual(self.drive('status').stdout.strip(), 'running')
        self.drive('stop')
        self.assertNotEqual(self.drive('status').returncode, 0)

    def test_start_requires_data(self):
        del self.env['WC3DATA']
        result = self.drive('start')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('WC3DATA', result.stderr)

    def test_verbs_need_a_running_game(self):
        result = self.drive('click', '10', '20')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('not running', result.stderr)

    def test_uiclick_types_console_command_and_checks_answer(self):
        self.attach()
        self.reply('case "$1" in "click Ok") echo "UICLICK ok name=Ok id=3 px=1,2 button=1";;'
                   ' *) echo "UICLICK fail name=${1#click } reason=hidden";; esac')
        self.assertEqual(self.drive('uiclick', 'Ok').returncode, 0)
        self.assertEqual(self.xdotool()[:3], ['key --clearmodifiers grave', 'type --delay 20 -- click Ok',
                                              'key --clearmodifiers Return'])
        result = self.drive('uiclick', 'Gone')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('reason=hidden', result.stdout)
        self.reply('echo "Unknown command \\"$1\\""')
        result = self.drive('uiclick', 'Ok')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('OPENREALM_UI_TEST=1', result.stderr)

    def test_ui_prints_only_dump_lines(self):
        self.attach()
        self.reply('echo noise; echo "UIDUMP screen=- window=1280x800 scene=0,0,0.8,0.6"; echo "' +
                   FRAME.format('Ok', 1, 1, 1) + '"; echo "UIDUMP end n=1"')
        lines = self.drive('ui', 'Ok').stdout.splitlines()
        self.assertEqual([line.split()[0] for line in lines], ['UIDUMP', 'UIFRAME', 'UIDUMP'])

    def test_uiwait_needs_visible_and_hittable_frame(self):
        self.attach()
        # Hidden on the first dump, ready on the second.
        self.reply(f'[ -e "$FAKE/seen" ] && v=1 || v=0; touch "$FAKE/seen"\n'
                   f'echo "{FRAME.format("Ok", "$v", 1, "$v")}"; echo "UIDUMP end n=1"')
        result = self.drive('uiwait', 'Ok', '20')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('ready', result.stdout)
        for visible, enabled, hit in [(1, 1, 0), (1, 0, 1), (0, 1, 1)]:
            self.reply(f'echo "{FRAME.format("Ok", visible, enabled, hit)}"; echo "UIDUMP end n=1"')
            result = self.drive('uiwait', 'Ok', '1')
            self.assertNotEqual(result.returncode, 0, (visible, enabled, hit))
            self.assertIn('not ready', result.stderr)

    def test_run_skips_comments_and_stops_at_first_failure(self):
        self.attach()
        self.reply('echo "UICLICK fail name=Bad reason=not-found"')
        scenario = self.fake / 'scenario.txt'
        scenario.write_text('# comment\n\nkey Escape\nwait 0.1\nuiclick Bad\nkey Return\n')
        result = self.drive('run', str(scenario))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('line 5: uiclick Bad', result.stderr)
        keys = [line for line in self.xdotool() if line.startswith('key') and 'grave' not in line]
        # The only Return is the console's; the scenario's own "key Return" after the failure never runs.
        self.assertEqual(keys, ['key --clearmodifiers Escape', 'key --clearmodifiers Return'])

    def test_waitlog_matches_rest_of_line_as_regex(self):
        self.attach()
        (self.state / 'game.log').write_text('noise\n  client lobby slot 1: occupied=1 client=1 type=human\n')
        result = self.drive('waitlog', '1', 'slot 1: occupied=1 .*type=human')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('client=1', result.stdout)
        result = self.drive('waitlog', '1', 'slot 2: occupied=1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no log line', result.stderr)
        self.assertNotEqual(self.drive('waitlog', 'slot').returncode, 0)

    def test_as_targets_named_second_instance(self):
        self.attach()
        guest = Path(f'{self.state}-guest')
        guest.mkdir()
        (guest / 'env').write_text('DISPLAY=:98\n')
        path = self.drive('as', 'guest', 'shot', 'g').stdout.strip()
        self.assertEqual(path, f'{guest}/shots/g.png')
        self.assertFalse((self.state / 'shots' / 'g.png').exists())
        for bad in [('as', '../x', 'shot'), ('as', 'guest')]:
            self.assertNotEqual(self.drive(*bad).returncode, 0, bad)

    def test_scenario_waitlog_and_as_lines(self):
        self.attach()
        guest = Path(f'{self.state}-guest')
        guest.mkdir()
        (guest / 'env').write_text('DISPLAY=:98\n')
        (self.state / 'game.log').write_text('G_ClientBegin: edict=0\n')
        scenario = self.fake / 'scenario.txt'
        scenario.write_text('waitlog 1 ^G_ClientBegin: edict=0\nas guest key F9\n')
        result = self.drive('run', str(scenario))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('key --clearmodifiers F9', self.xdotool())

    def test_shot_prints_written_file(self):
        self.attach()
        path = self.drive('shot', 'menu').stdout.strip()
        self.assertEqual(path, f'{self.state}/shots/menu.png')
        self.assertEqual(Path(path).read_text(), 'PNG')


if __name__ == '__main__':
    unittest.main()
