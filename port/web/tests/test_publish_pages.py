"""Synthetic Pages asset packaging: no binaries from proprietary game archives."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'publish-pages.py'
spec = importlib.util.spec_from_file_location('publish_pages', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PublishPagesTest(unittest.TestCase):
    def test_copies_runtime_not_build_intermediates_or_retail_game_data(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build, site, assets = root / 'build', root / 'site/play', root / 'assets'
            build.mkdir()
            assets.mkdir()
            for name in module.REQUIRED:
                (build / name).write_bytes(b'fixture')
            for name in module.PAGES_ASSETS:
                (assets / name).write_bytes(b'fixture')
            for name in ('engine_unity.c', 'engine.o', 'War3.mpq', 'index.worker.js', 'index.data'):
                (build / name).write_bytes(b'fixture')
            module.publish(build, site, assets)
            self.assertTrue((site / 'index.wasm').exists())
            self.assertTrue((site / 'index.worker.js').exists())
            self.assertTrue((site / 'index.data').exists())
            for name in ('engine_unity.c', 'engine.o', 'War3.mpq'):
                self.assertFalse((site / name).exists(), name)
            self.assertTrue((site / 'coi-serviceworker.js').exists())

    def test_rejects_missing_runtime(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            src, dest, assets = root / 'src', root / 'site/play', root / 'assets'
            src.mkdir()
            assets.mkdir()
            with self.assertRaisesRegex(RuntimeError, 'missing browser runtime asset'):
                module.publish(src, dest, assets)


if __name__ == '__main__':
    unittest.main()
