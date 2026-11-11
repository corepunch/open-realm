"""Exercise the production Make schemas in an isolated, compiler-free build."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestBuild(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ow3-test-build-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        makefile = (ROOT / 'Makefile').read_text()
        schemas = '\n'.join(re.findall(
            r'^define test_(?:fixture_)?schema\n.*?^endef', makefile, re.M | re.S))
        (self.root / 'fixtures/nested').mkdir(parents=True)
        for name in ('fixtures/nested/input', 'source.c', 'server.h', 'nested.h', 'library', 'generator', 'recipe.mk'):
            (self.root / name).write_text('input\n')
        (self.root / 'compiler.py').write_text('''import pathlib, sys
if '-MM' in sys.argv:
    print(sys.argv[sys.argv.index('-MT') + 1] + ': nested.h')
    sys.exit(0)
with open('events', 'a') as f: f.write('compile\\n')
out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
out.write_text('#!/bin/sh\\ntest -f archive || exit 1\\necho run >> events\\n')
out.chmod(0o755)
''')
        (self.root / 'Makefile').write_text(schemas + '''
.DEFAULT_GOAL := suite
CC := python3 compiler.py
BIN_DIR := bin
TEST_JUNIT_DIR := junit
SERVER_HEADERS := server.h
$(BIN_DIR) $(TEST_JUNIT_DIR):
	@mkdir -p $@
$(eval $(call test_fixture_schema,assets,archive,fixtures,generator,recipe.mk))
archive:
	@echo pack >> events
	@find fixtures -type f | sort > $@
	@test "$(FAIL)" != 1
.PHONY: assets suite suite-other
assets:
	@echo verify >> events
$(eval $(call test_schema,suite,library,,bin/test,source.c,,,assets))
$(eval $(call test_schema,suite-other,library,,bin/other,source.c,,,assets))
''')

    def make(self, *args, success=True):
        # Do not inherit the outer suite's jobserver or command-line variables.
        env = {k: v for k, v in os.environ.items()
               if k not in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL')}
        result = subprocess.run(['make', '--no-print-directory', *args],
                                cwd=self.root, env=env, text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def events(self, name):
        return (self.root / 'events').read_text().splitlines().count(name)

    def age_inputs(self):
        # Deterministic timestamps even on filesystems with one-second precision.
        for path in self.root.rglob('*'):
            os.utime(path, (1000, 1000))
        for name in ('archive', 'bin/test', 'bin/other'):
            path = self.root / name
            if path.exists():
                os.utime(path, (2000, 2000))

    def test_warm_run_executes_and_verifies_without_compiling_or_packing(self):
        self.make()
        self.make()
        self.assertEqual(self.events('compile'), 1)
        self.assertEqual(self.events('pack'), 1)
        self.assertEqual(self.events('run'), 2)
        self.assertEqual(self.events('verify'), 2)

    def test_runtime_fixture_edits_do_not_recompile(self):
        self.make()
        for i, name in enumerate(('fixtures/nested/input', 'generator', 'recipe.mk'), 2):
            self.age_inputs()
            (self.root / name).write_text('changed\n')
            self.make()
            self.assertEqual(self.events('pack'), i, name)
            self.assertEqual(self.events('compile'), 1, name)

    def test_added_and_deleted_fixture_paths_repack(self):
        self.make()
        self.age_inputs()
        extra = self.root / 'fixtures/nested/extra'
        extra.write_text('extra\n')
        self.make()
        self.assertIn('fixtures/nested/extra', (self.root / 'archive').read_text())
        self.age_inputs()
        extra.unlink()
        self.make()
        self.assertNotIn('fixtures/nested/extra', (self.root / 'archive').read_text())
        self.age_inputs()
        (self.root / 'fixtures/nested/input').unlink()
        (self.root / 'fixtures/nested').rmdir()
        self.make()
        self.assertEqual(self.events('pack'), 4)
        self.assertEqual(self.events('compile'), 1)

    def test_missing_archive_is_regenerated(self):
        self.make()
        (self.root / 'archive').unlink()
        self.make()
        self.assertEqual(self.events('pack'), 2)
        self.assertEqual(self.events('compile'), 1)

    def test_missing_dependency_file_rebuilds_only_once(self):
        self.make()
        (self.root / 'bin/test.d').unlink()
        self.make()
        self.make()
        self.assertTrue((self.root / 'bin/test.d').is_file())
        self.assertEqual(self.events('compile'), 2)
        self.assertEqual(self.events('run'), 3)

    def test_fixture_only_target_does_not_compile_tests(self):
        self.make('assets')
        self.assertEqual(self.events('pack'), 1)
        self.assertEqual(self.events('compile'), 0)

    def test_build_recipe_edit_recompiles(self):
        self.make()
        self.age_inputs()
        path = self.root / 'Makefile'
        path.write_text(path.read_text() + '\n# Changed build recipe\n')
        self.make()
        self.assertEqual(self.events('compile'), 2)

    def test_compile_inputs_still_rebuild(self):
        self.make()
        for i, name in enumerate(('source.c', 'server.h', 'nested.h', 'library'), 2):
            self.age_inputs()
            (self.root / name).write_text('changed\n')
            self.make()
            self.assertEqual(self.events('compile'), i, name)
            self.assertEqual(self.events('pack'), 1, name)

    def test_parallel_consumers_wait_for_one_archive_build(self):
        self.make('-j4', 'suite', 'suite-other')
        self.assertEqual(self.events('pack'), 1)
        self.assertEqual(self.events('compile'), 2)
        self.assertEqual(self.events('run'), 2)

    def test_failed_pack_is_removed_and_retried(self):
        self.make('FAIL=1', success=False)
        self.assertFalse((self.root / 'archive').exists())
        self.make()
        self.assertEqual(self.events('pack'), 2)
        self.assertEqual(self.events('run'), 1)


if __name__ == '__main__':
    unittest.main()
