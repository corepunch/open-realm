"""SC2 archive discovery, authored names, and launcher command contracts."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/parity'))
from map_picker import pick_map

CATALOG = ROOT / 'tools/parity/sc2_maps.py'
LAUNCHER = ROOT / 'tools/parity/sc2.sh'
TOOL = ROOT / 'build/bin/mpqtool'


class SC2ParityMaps(unittest.TestCase):
    def test_archive_catalog_and_dry_run(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            archive = data / 'Campaigns/Liberty.SC2Campaign/Base.SC2Maps'
            archive.parent.mkdir(parents=True)
            name = data / 'name.txt'
            name.write_text('DocInfo/Name=Liberation Day\n')
            info = data / 'MapInfo'
            info.write_bytes(b'IpaM')
            map_path = 'Maps/Campaign/TRaynor01.SC2Map'
            subprocess.run([str(TOOL), '-mpq', str(archive), 'pack', str(name),
                            map_path + '/enUS.SC2Data/LocalizedData/GameStrings.txt',
                            str(info), map_path + '/MapInfo'], check=True, capture_output=True)
            listed = subprocess.run([sys.executable, str(CATALOG), '--data', str(data), '--list'],
                                    text=True, capture_output=True, check=True)
            self.assertIn('Liberation Day', listed.stdout)
            self.assertIn('sc2-traynor01', listed.stdout)
            resolved = subprocess.run([sys.executable, str(CATALOG), '--data', str(data),
                                       '--resolve', 'traynor01'], text=True, capture_output=True, check=True)
            self.assertEqual(resolved.stdout.strip(), map_path)
            env = dict(os.environ, SC2DATA=str(data), SC2_BINARY=sys.executable, SC2_DRY_RUN='1')
            launch = subprocess.run(['/bin/bash', str(LAUNCHER), '--map=traynor01'], env=env,
                                    text=True, capture_output=True, check=True)
            command = shlex.split(launch.stdout.split('Command: ', 1)[1])
            self.assertEqual(command[command.index('+map') + 1], map_path)
            bad = subprocess.run(['/bin/bash', str(LAUNCHER), '--map=unknown'], env=env,
                                 text=True, capture_output=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('Unknown map', bad.stderr)

    def test_loose_map_and_missing_name_are_visible(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            folder = data / 'Maps/Custom/Small.SC2Map'
            folder.mkdir(parents=True)
            (folder / 'MapInfo').write_bytes(b'IpaM')
            components = data / 'Maps/Custom/Other.SC2Components'
            components.mkdir()
            (components / 'MapInfo.xml').write_text('<MapInfo><Name value="Other"/></MapInfo>')
            result = subprocess.run([sys.executable, str(CATALOG), '--data', str(data), '--list'],
                                    text=True, capture_output=True, check=True)
            self.assertIn('sc2-small', result.stdout)
            self.assertIn('sc2-other', result.stdout)
            self.assertIn('using map identifier', result.stderr)
            env = dict(os.environ, SC2DATA=str(data), SC2_BINARY=sys.executable, SC2_DRY_RUN='1')
            launch = subprocess.run(['/bin/bash', str(LAUNCHER), '--map=small'], env=env,
                                    text=True, capture_output=True, check=True)
            command = shlex.split(launch.stdout.split('Command: ', 1)[1])
            self.assertEqual(command[command.index('+map') + 1], 'Maps/Custom/Small.SC2Map')

    def test_shared_picker_uses_sc2_heading_and_selects_row(self):
        row = dict(edition='sc2', slug='traynor01', name='Liberation Day', title='',
                   campaign='Liberty', path='Maps/Campaign/TRaynor01.SC2Map')
        screen = mock.Mock()
        screen.getmaxyx.return_value = (30, 100)
        screen.get_wch.side_effect = ['\n']
        with mock.patch('map_picker.curses.curs_set'), \
             mock.patch('map_picker.curses.has_colors', return_value=False):
            selected = pick_map(screen, [row], heading='S T A R C R A F T   I I',
                                subtitle='MAP ATLAS', compact_heading='STARCRAFT II')
        self.assertEqual(selected, row)
        self.assertTrue(any(call.args[2] == 'S T A R C R A F T   I I'
                            for call in screen.addnstr.call_args_list))


if __name__ == '__main__':
    unittest.main()
