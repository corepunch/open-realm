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

    def test_default_build_failure_prevents_loading_stale_modules(self):
        with tempfile.TemporaryDirectory() as directory:
            # A clean checkout has no default binary; local builds must not mask that case.
            launcher = Path(directory, 'tools/parity/wc3.sh')
            launcher.parent.mkdir(parents=True)
            launcher.write_bytes((ROOT / 'tools/parity/wc3.sh').read_bytes())
            bindir = Path(directory, 'bin')
            bindir.mkdir()
            make = bindir / 'make'
            make.write_text('#!/bin/sh\necho refresh-default-build >&2\nexit 37\n')
            make.chmod(0o755)
            env = dict(os.environ, WC3DATA=directory, WC3_DRY_RUN='0',
                       PATH=str(bindir) + os.pathsep + os.environ['PATH'])
            env.pop('WC3_BINARY', None)
            result = subprocess.run(['bash', str(launcher),
                                     'openrealm', '--map=menu'], env=env,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 37)
            self.assertIn('refresh-default-build', result.stderr)
            self.assertNotIn('Server initialization', result.stdout)
            env['WC3_DRY_RUN'] = '1'
            result = subprocess.run(['bash', str(launcher),
                                     'openrealm', '--map=menu'], env=env,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('refresh-default-build', result.stderr)
            cmd = shlex.split(result.stdout.split('Command: ', 1)[1])
            self.assertEqual(cmd[0], str(Path(directory, 'build/bin/openwarcraft3')))
            self.assertIn('+menu_main', cmd)

    def test_invalid_arguments(self):
        for args in [('openrealm', '--map=typo'), ('openrealm', '--map='),
                     ('openrealm', '--oops'), ('openrealm', 'tft', 'roc'),
                     ('openrealm', '--map=menu', '--map=elf1')]:
            self.launch(*args, ok=False)


if __name__ == '__main__':
    unittest.main()
