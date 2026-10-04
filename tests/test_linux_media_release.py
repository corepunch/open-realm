#!/usr/bin/env python3
"""Linux media packaging regressions; no FFmpeg or Linux host required."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("linux_media", ROOT / "dist-scripts/linux/bundle_media.py")
media = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(media)


class LinuxMediaReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.library = self.root / "Téléchargements/Games WIP/system"
        self.library.mkdir(parents=True)
        self.binary = self.root / "openwarcraft3"
        self.binary.write_bytes(b"executable fixture")
        self.destination = self.root / "release/lib"
        self.docs = self.root / "doc"
        self.commands = []

    def library_file(self, name):
        path = self.library / name
        path.write_bytes(name.encode())
        package = "fixture-" + name.split(".")[0]
        notice = self.docs / package / "copyright"
        notice.parent.mkdir(parents=True, exist_ok=True)
        notice.write_text(package + " license")
        return path

    def run_tool(self, command, **kwargs):
        self.commands.append(command)
        if command[0] == "ldd":
            paths = self.roots if command[1:] == [str(self.binary)] else self.closure
            output = "".join(f"{p.name} => {p} (0x1234)\n" for p in paths)
        elif command[0] == "dpkg-query":
            output = "fixture-" + Path(command[-1]).name.split(".")[0] + ":amd64: " + command[-1]
        else:
            output = ""
        return subprocess.CompletedProcess(command, 0, output, "")

    def test_bundles_media_closure_with_local_rpaths_and_licenses(self):
        self.roots = [self.library_file(name + ".so.60") for name in media.REQUIRED]
        codec = self.library_file("libcodec-support.so.1")
        libc = self.library_file("libc.so.6")
        driver = self.library_file("libdrm.so.2")
        cpp = self.library_file("libstdc++.so.6")
        self.closure = self.roots + [codec, libc, driver, cpp]
        with patch.object(media, "DOC_ROOT", self.docs), patch.object(media.subprocess, "run", self.run_tool):
            media.bundle(self.binary, self.destination)
        for path in self.roots + [codec]:
            self.assertEqual((self.destination / path.name).read_bytes(), path.read_bytes())
            self.assertIn(["patchelf", "--set-rpath", "$ORIGIN", str(self.destination / path.name)], self.commands)
        self.assertFalse((self.destination / libc.name).exists())
        self.assertFalse((self.destination / driver.name).exists())
        self.assertFalse((self.destination / cpp.name).exists())
        self.assertEqual(len(list((self.destination.parent / "licenses/ffmpeg").glob("*/copyright"))), 6)

    def test_disabled_decoder_fails_before_packaging(self):
        self.roots = []
        with patch.object(media.subprocess, "run", self.run_tool):
            with self.assertRaisesRegex(RuntimeError, "FFMPEG=1"):
                media.bundle(self.binary, self.destination)
        self.assertFalse(self.destination.exists())

    def test_unresolved_runtime_fails_before_packaging(self):
        result = subprocess.CompletedProcess([], 0, "libavcodec.so.60 => not found\n", "")
        with patch.object(media.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(RuntimeError, "not found"):
                media.bundle(self.binary, self.destination)
        self.assertFalse(self.destination.exists())

    def test_linux_release_enables_music_decoder(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        linux = workflow.split("- name: Build (Linux)\n", 1)[1].split("- name:", 1)[0]
        self.assertIn("FFMPEG=1", linux)

    def test_missing_copyright_notice_fails_before_packaging(self):
        self.roots = [self.library_file(name + ".so.60") for name in media.REQUIRED]
        self.closure = self.roots
        for notice in self.docs.glob("*/copyright"):
            notice.unlink()
        with patch.object(media, "DOC_ROOT", self.docs), patch.object(media.subprocess, "run", self.run_tool):
            with self.assertRaisesRegex(RuntimeError, "Missing copyright"):
                media.bundle(self.binary, self.destination)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
