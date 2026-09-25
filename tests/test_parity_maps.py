"""Campaign catalog and searchable picker contracts without retail data."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('parity_maps', ROOT / 'tools/parity/wc3_maps.py')
maps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(maps)


class CampaignMaps(unittest.TestCase):
    def test_roc_and_tft_names_and_interludes(self):
        roc = '[Human]\nName="Campaign"\nTitle0="Interlude"\nMission0="A Meeting"\nFile0="Human02Interlude"\n'
        tft = '[Undead]\nName="Campaign"\nMission0="Chapter Seven, Part Two","The Forgotten Ones","Maps\\FrozenThrone\\Campaign\\UndeadX07b.w3x"\n'
        rows = maps.parse_campaign(roc, 'roc') + maps.parse_campaign(tft, 'tft')
        self.assertEqual(rows[0]['slug'], 'human2-interlude')
        self.assertEqual(rows[0]['name'], 'A Meeting')
        self.assertEqual(rows[1]['slug'], 'undead7b')
        self.assertEqual(rows[1]['title'], 'Chapter Seven, Part Two')

    def test_non_map_cinematic_is_not_a_map(self):
        rows = maps.parse_campaign('[Undead]\nMission0="Finale","Fight","Doodads\\Fight.mdl"\n', 'tft')
        self.assertEqual(rows, [])

    def test_authored_bonus_zone_references(self):
        script = r'''
set udg_ZoneMapPath = "Maps\\FrozenThrone\\Campaign\\"
set udg_ZoneMapExt = ".w3x"
set udg_ZoneMaps[2] = "OrcX02_02"
call SetNextLevelBJ("Maps\\FrozenThrone\\Campaign\\OrcX03a.w3x")
'''
        self.assertEqual(set(maps.script_maps(script)), {
            'Maps/FrozenThrone/Campaign/OrcX02_02.w3x',
            'Maps/FrozenThrone/Campaign/OrcX03a.w3x'})

    def test_aliases_preserve_variants(self):
        for name, slug in [('NightElfX08Finale', 'elf8-finale'), ('OrcX02_10', 'orc2-10'),
                           ('HumanX03Secret', 'human3-secret'), ('Prologue02', 'prologue2')]:
            self.assertEqual(maps.map_slug(name), slug)

    def test_search_matches_all_words_across_metadata(self):
        rows = [dict(edition='tft', slug='elf1', name='Rise of the Naga', title='Chapter One',
                     campaign='Terror of the Tides', path='Maps/NightElfX01.w3x'),
                dict(edition='roc', slug='elf1', name='Enemies at the Gate', title='Chapter One',
                     campaign="Eternity's End", path='Maps/NightElf01.w3m')]
        self.assertEqual(maps.filter_maps(rows, 'NAGA tft'), rows[:1])
        self.assertEqual(maps.filter_maps(rows, 'roc gate'), rows[1:])
        self.assertEqual(maps.resolve_map(rows, 'roc-elf1', 'tft'), rows[1])
        with self.assertRaises(ValueError):
            maps.resolve_map(rows, 'typo', 'tft')

    def test_catalog_reads_mpqs_and_optional_maps_and_refreshes(self):
        tool = ROOT / 'build/bin/mpqtool'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            info = struct.pack('<III', 25, 1, 1) + b'Internal\0Author\0Description\0Any\0' + bytes(65)
            info += b'\0\0TRIGSTR_1\0\0'
            (root / 'info').write_bytes(info)
            (root / 'strings').write_text('STRING 1\n{\nAuthored Custom Name\n}\n')
            nested = root / 'nested.w3m'
            subprocess.run([str(tool), '-mpq', str(nested), 'pack', str(root / 'info'), 'war3map.w3i',
                            str(root / 'strings'), 'war3map.wts'], check=True, capture_output=True)
            table = root / 'campaign.txt'
            table.write_text('[Human]\nName="Campaign"\nTitle0="Chapter One"\nMission0="Authored Chapter"\nFile0="Human01"\n')
            archive = root / 'war3.mpq'
            subprocess.run([str(tool), '-mpq', str(archive), 'pack', str(table), 'UI/CampaignStrings.txt',
                            str(nested), 'Maps/Campaign/Human01.w3m'], check=True, capture_output=True)
            optional = root / 'Optional Maps'
            optional.mkdir()
            (optional / 'First.w3m').write_bytes(nested.read_bytes())
            with mock.patch.object(maps, 'ROOT', root), mock.patch.dict(maps.os.environ, {}, clear=True):
                rows = maps.load_catalog(root, tool, map_dirs=[optional])
                self.assertEqual(len(rows), 2)
                self.assertEqual(rows[0]['name'], 'Authored Chapter')
                self.assertEqual(rows[1]['name'], 'Authored Custom Name')
                self.assertEqual(rows[1]['path'], 'Optional Maps/First.w3m')
                with mock.patch.object(maps, 'generate_catalog', side_effect=AssertionError('cache miss')):
                    self.assertEqual(maps.load_catalog(root, tool, map_dirs=[optional]), rows)
                (optional / 'Second.w3m').write_bytes(nested.read_bytes())
                self.assertEqual(len(maps.load_catalog(root, tool, map_dirs=[optional])), 3)

    def test_picker_search_enter_cancel_and_resize(self):
        rows = [dict(maps.MENU), dict(edition='tft', slug='elf1', name='Rise of the Naga',
                                     title='Chapter One', campaign='Terror of the Tides', path='Maps/Elf.w3x')]
        screen = mock.Mock()
        screen.getmaxyx.return_value = (15, 90)
        screen.get_wch.side_effect = [maps.curses.KEY_RESIZE, 'n', 'a', 'g', 'a', '\n']
        with mock.patch.object(maps.curses, 'curs_set'), mock.patch.object(maps.curses, 'has_colors', return_value=False):
            self.assertEqual(maps.pick_map(screen, rows), rows[1])
            screen.getmaxyx.return_value = (2, 5)
            screen.get_wch.side_effect = ['z', '\n', '\x15', '\x1b']
            self.assertIsNone(maps.pick_map(screen, rows))

    def test_picker_color_and_monochrome_themes(self):
        with mock.patch.object(maps.curses, 'has_colors', return_value=False):
            self.assertEqual(maps.picker_theme()['selected'], maps.curses.A_REVERSE | maps.curses.A_BOLD)
        for colors in (8, 256):
            with mock.patch.multiple(maps.curses, has_colors=mock.Mock(return_value=True),
                                     start_color=mock.Mock(), use_default_colors=mock.Mock(),
                                     init_pair=mock.Mock(), color_pair=mock.Mock(side_effect=lambda n: n << 8)), \
                    mock.patch.object(maps.curses, 'COLORS', colors, create=True):
                theme = maps.picker_theme()
                self.assertEqual(maps.curses.init_pair.call_count, 6)
                self.assertNotEqual(theme['roc'], theme['tft'])
                for call in maps.curses.init_pair.call_args_list:
                    _, foreground, background = call.args
                    self.assertLess(foreground, colors)
                    self.assertLess(background, colors)

    def test_picker_layout_highlight_and_empty_state(self):
        screen = mock.Mock()
        screen.getmaxyx.return_value = (30, 110)
        theme = dict.fromkeys(['text', 'muted', 'accent', 'roc', 'tft'], 0)
        theme['selected'] = 99
        row = dict(maps.MENU)
        maps.draw_picker(screen, [row], '', 0, 98, theme)
        calls = screen.addnstr.call_args_list
        self.assertTrue(any(c.args[2] == 'W A R C R A F T   I I I' for c in calls))
        self.assertTrue(any(c.args[2] == 'Main Menu' and c.args[4] == 99 for c in calls))
        screen.reset_mock()
        maps.draw_picker(screen, [], 'missing', 0, 98, theme)
        self.assertTrue(any('No maps match' in c.args[2] for c in screen.addnstr.call_args_list))


if __name__ == '__main__':
    unittest.main()
