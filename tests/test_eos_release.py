#!/usr/bin/env python3
"""SDK release preparation regressions; no credentials or real SDK required."""

import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("eos_release", ROOT / "dist-scripts/eos/prepare_release.py")
eos = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(eos)


class EOSReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.archive = self.root / "sdk.zip"
        self.output = self.root / "data/eos"
        self.environment = {key: "fixture-" + key.lower() for key in eos.CONFIG_KEYS}

    def sdk_archive(self, extra=None, omit=None):
        entries = {
            "SDK/Include/eos_sdk.h": b"header fixture",
            "SDK/Include/subdir/eos_types.h": b"nested header fixture",
            eos.NOTICE: b"notice fixture",
            "SDK/Tools/unneeded.exe": b"excluded tools",
            "Samples/Sample.cpp": b"excluded sample",
        }
        for runtime in eos.RUNTIMES.values():
            entries["SDK/Bin/" + runtime] = b"runtime fixture"
        with ZipFile(self.archive, "w") as sdk:
            for name, content in entries.items():
                if name != omit:
                    sdk.writestr(name, content)
            if extra is not None:
                sdk.writestr(extra, b"unsafe fixture")
        return hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def test_each_platform_extracts_only_headers_runtime_notice_and_private_config(self):
        digest = self.sdk_archive()
        for platform, runtime in eos.RUNTIMES.items():
            with self.subTest(platform=platform):
                output = self.root / platform
                eos.prepare(self.archive, output, platform, self.environment, digest)
                self.assertEqual((output / "SDK/Bin" / runtime).read_bytes(), b"runtime fixture")
                self.assertEqual(len(list((output / "SDK/Bin").iterdir())), 1)
                self.assertTrue((output / "SDK/Include/subdir/eos_types.h").is_file())
                self.assertEqual((output / eos.NOTICE).read_bytes(), b"notice fixture")
                self.assertFalse((output / "SDK/Tools").exists())
                self.assertFalse((output / "Samples").exists())
                self.assertEqual((output / "eos.cfg").read_text(), eos.config_text(self.environment))
                if eos.os.name != "nt":
                    self.assertEqual((output / "eos.cfg").stat().st_mode & 0o777, 0o600)

    def test_wrong_checksum_does_not_install_sdk_or_config(self):
        self.sdk_archive()
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            eos.prepare(self.archive, self.output, "Linux", self.environment)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_sdk_only_mirror_uses_its_pinned_checksum(self):
        digest = self.sdk_archive()
        with patch.object(eos, "SDK_RELEASE_SHA256", digest):
            eos.prepare(self.archive, self.output, "Linux", self.environment)
            self.assertTrue((self.output / "SDK/Bin" / eos.RUNTIMES["Linux"]).is_file())
            with self.archive.open("ab") as archive:
                archive.write(b"modified mirror contents")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                eos.prepare(self.archive, self.root / "tampered", "Linux", self.environment)
            self.assertFalse((self.root / "tampered").exists())

    def test_missing_runtime_or_notices_fails_before_installation(self):
        for name in (eos.NOTICE, "SDK/Bin/" + eos.RUNTIMES["Windows"], "SDK/Include/eos_sdk.h"):
            digest = self.sdk_archive(omit=name)
            with self.assertRaisesRegex(ValueError, "lacks required"):
                eos.prepare(self.archive, self.output, "Windows", self.environment, digest)
            self.assertFalse(self.output.exists())

    def test_archive_traversal_and_symlinks_fail_before_installation(self):
        symlink = ZipInfo("SDK/Include/symlink.h")
        symlink.create_system = 3
        symlink.external_attr = 0o120777 << 16
        for name in ("SDK/Include/../../escape", "SDK/Include/evil\\file.h", symlink):
            digest = self.sdk_archive(extra=name)
            with self.assertRaisesRegex(ValueError, "Unsafe path"):
                eos.prepare(self.archive, self.output, "Linux", self.environment, digest)
            self.assertFalse(self.output.exists())
            self.assertFalse((self.root / "escape").exists())

    def test_missing_or_malformed_config_never_prints_secret_values(self):
        for value in ("", "secret\nINJECTED=value", "secret value", "s" * 128):
            environment = dict(self.environment, OPENREALM_EOS_CLIENT_SECRET=value)
            with self.assertRaisesRegex(ValueError, "OPENREALM_EOS_CLIENT_SECRET") as caught:
                eos.prepare(None, self.output, "Linux", environment)
            if value:
                self.assertNotIn(value, str(caught.exception))
            self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        digest = self.sdk_archive()
        self.output.mkdir(parents=True)
        config = self.output / "eos.cfg"
        config.write_text("existing configuration")
        with self.assertRaisesRegex(ValueError, "already exists"):
            eos.prepare(self.archive, self.output, "Linux", self.environment, digest)
        self.assertEqual(config.read_text(), "existing configuration")

    def test_download_url_is_required_and_https_only(self):
        with self.assertRaisesRegex(ValueError, "EOS_SDK_DOWNLOAD_URL"):
            eos.prepare(None, self.output, "Linux", self.environment)
        for url in ("http://example.com/sdk", "file:///sdk.zip", "https://user:secret@example.com/sdk"):
            with self.assertRaisesRegex(ValueError, "HTTPS archive URL"):
                eos.require_https(url)
        eos.require_https("https://example.com/sdk?signature=fixture")

    def test_download_failures_redact_signed_url(self):
        url = "https://example.com/sdk?signature=secret-fixture"
        with patch.object(eos, "build_opener") as opener:
            opener.return_value.open.side_effect = OSError(url)
            with self.assertRaisesRegex(ValueError, "download failed") as caught:
                eos.download_sdk(url, self.archive)
        self.assertNotIn(url, str(caught.exception))
        self.assertNotIn("secret-fixture", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
