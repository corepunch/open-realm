"""Command-contract tests; no Wine, display, or retail archives required."""
import os
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ParityLauncherTest(unittest.TestCase):
    def launch(self, *args, ok=True):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, 'war3.exe').touch()
            rows = []
            for edition, folder, ext, suffix in [('tft', 'Maps/FrozenThrone/Campaign', 'w3x', 'X01'),
                                                  ('roc', 'Maps/Campaign', 'w3m', '01')]:
                for slug, name in [('elf1', 'NightElf'), ('human1', 'Human'), ('undead1', 'Undead'), ('orc1', 'Orc')]:
                    rows.append(dict(edition=edition, slug=slug, path=f'{folder}/{name}{suffix}.{ext}',
                                     name='Rise of the Naga' if slug == 'elf1' else name,
                                     title='Chapter One', campaign='Campaign'))
            catalog = Path(directory, 'catalog.json')
            catalog.write_text(json.dumps(dict(maps=rows)))
            env = dict(os.environ, WC3_MAP_CATALOG=str(catalog), WC3DATA=directory, WC3_BINARY=sys.executable,
                       WINE=sys.executable, WINEPREFIX=directory, WC3_DRY_RUN='1')
            result = subprocess.run(['bash', str(ROOT / 'tools/parity/wc3.sh'), *args],
                                    env=env, text=True, capture_output=True)
        if not ok:
            self.assertNotEqual(result.returncode, 0)
            return result.stderr
        self.assertEqual(result.returncode, 0, result.stderr)
        return shlex.split(result.stdout.split('Command: ', 1)[1]) if 'Command: ' in result.stdout else result.stdout

    def test_default_and_explicit_menu(self):
        for args in [('openrealm',), ('openrealm', '--map=menu')]:
            cmd = self.launch(*args)
            self.assertIn('+menu_main', cmd)
            self.assertNotIn('+map', cmd)

    def test_campaign_aliases(self):
        for edition, folder, ext, suffix in [('tft', 'Maps/FrozenThrone/Campaign', 'w3x', 'X01'),
                                              ('roc', 'Maps/Campaign', 'w3m', '01')]:
            for slug, name in [('elf1', 'NightElf'), ('human1', 'Human'),
                               ('undead1', 'Undead'), ('orc1', 'Orc')]:
                cmd = self.launch('openrealm', edition, '--map=' + slug)
                self.assertEqual(cmd[cmd.index('+map') + 1], f'{folder}/{name}{suffix}.{ext}')
                self.assertEqual(cmd[cmd.index('skip_cutscene') + 1], '1')

    def test_intro_opt_in(self):
        cmd = self.launch('openrealm', '--map=elf1', '--intro')
        self.assertEqual(cmd[cmd.index('skip_cutscene') + 1], '0')

    def test_custom_map_and_legacy_syntax(self):
        path = 'Maps/My custom map.w3x'
        for args in [('openrealm', 'tft', path), ('openrealm', '--map=' + path)]:
            cmd = self.launch(*args)
            self.assertEqual(cmd[cmd.index('+map') + 1], path)
            self.assertEqual(cmd[cmd.index('skip_cutscene') + 1], '0')

    def test_retail(self):
        self.assertNotIn('-loadfile', self.launch('retail'))
        cmd = self.launch('retail', 'roc', '--map=elf1')
        self.assertIn('-classic', cmd)
        self.assertEqual(cmd[cmd.index('-loadfile') + 1], 'Maps\\Campaign\\NightElf01.w3m')
        self.assertNotIn('skip_cutscene', cmd)

    def test_listing_and_qualified_alias(self):
        listing = self.launch('openrealm', '--list')
        self.assertIn('Rise of the Naga', listing)
        self.assertIn('roc-elf1', listing)
        cmd = self.launch('openrealm', '--map=roc-elf1')
        self.assertEqual(cmd[cmd.index('fs_expansion') + 1], '0')
        self.assertEqual(cmd[cmd.index('+map') + 1], 'Maps/Campaign/NightElf01.w3m')

    def test_invalid_arguments(self):
        for args in [('openrealm', '--map=typo'), ('openrealm', '--map='),
                     ('openrealm', '--oops'), ('openrealm', 'tft', 'roc'),
                     ('openrealm', '--map=menu', '--map=elf1')]:
            self.launch(*args, ok=False)


if __name__ == '__main__':
    unittest.main()
