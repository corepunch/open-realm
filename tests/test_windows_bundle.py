#!/usr/bin/env python3
"""Windows DLL bundling regressions; no Windows or MSYS2 host required.

A fake `ldd` resolves each binary's dependencies (listed in "<binary>.deps")
through PATH and a fake MSYS2 prefix, like the MSYS2 ldd does through the
Windows loader. A fake `cygpath` maps "G:/x" to "<root>/g/x", where "g" is a
symlink to the directory "G:", so one directory has a drive form with a
colon and a POSIX form without one, as on MSYS2.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "dist-scripts/windows/bundle_mingw_dlls.sh"

FAKE_LDD = r"""#!/usr/bin/env bash
for bin in "$@"; do
    echo "$bin:"
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        hit=""
        IFS=: read -r -a dirs <<< "$PATH:$FAKE_SYSTEM_DIR:$FAKE_PREFIX_DIR"
        for d in "${dirs[@]}"; do
            if [ -n "$d" ] && [ -f "$d/$dep" ]; then hit="$d/$dep"; break; fi
        done
        if [ -n "$hit" ]; then
            printf '\t%s => %s (0x7ff800000000)\n' "$dep" "$hit"
        else
            printf '\t%s => not found\n' "$dep"
        fi
    done < "$bin.deps"
done
"""

FAKE_CYGPATH = r"""#!/usr/bin/env bash
[ "$1" = "-u" ] || exit 2
case "$2" in
    G:/*) printf '%s/g/%s\n' "$FAKE_ROOT" "${2#G:/}" ;;
    *) printf '%s\n' "$2" ;;
esac
"""


class WindowsBundleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.tools = self.root / "tools"
        self.prefix = self.root / "ucrt64/bin"
        self.system = self.root / "Windows/System32"
        self.build = self.root / "build"
        for d in (self.tools, self.prefix, self.system, self.build):
            d.mkdir(parents=True)
        self.tool("ldd", FAKE_LDD)
        (self.system / "KERNEL32.dll").write_bytes(b"system")
        for name in ("SDL2.dll", "libepoxy-0.dll", "libgcc_s_seh-1.dll"):
            (self.prefix / name).write_bytes(name.encode())

    def tool(self, name, text):
        path = self.tools / name
        path.write_text(text)
        path.chmod(0o755)

    def binary(self, name, deps):
        path = self.build / name
        path.write_bytes(b"MZ")
        (self.build / (name + ".deps")).write_text("\n".join(deps) + "\n")
        return path

    def run_bundle(self, dest, *binaries):
        env = dict(os.environ)
        env["PATH"] = str(self.tools) + ":" + env.get("PATH", "/usr/bin:/bin")
        env["FAKE_ROOT"] = str(self.root)
        env["FAKE_PREFIX_DIR"] = str(self.prefix)
        env["FAKE_SYSTEM_DIR"] = str(self.system)
        env["BZ_MINGW_PREFIX_RE"] = "^" + re.escape(str(self.prefix)) + "/"
        return subprocess.run(
            ["bash", str(SCRIPT), str(dest), *map(str, binaries)],
            cwd=self.root, env=env, capture_output=True, text=True)

    def runtime_deps(self, zlib):
        return ["SDL2.dll", zlib, "libepoxy-0.dll", "KERNEL32.dll"]

    def assertBundled(self, result, dest, names):
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in names:
            self.assertTrue((dest / name).is_file(), name)
        self.assertFalse((dest / "KERNEL32.dll").exists())

    def test_zlib_from_msys2_zlib_1_4_is_accepted(self):
        (self.prefix / "libz-1.dll").write_bytes(b"zlib")
        exe = self.binary("openwarcraft3.exe", self.runtime_deps("libz-1.dll"))
        dest = self.root / "pkg"
        result = self.run_bundle(dest, exe)
        self.assertBundled(result, dest, ["SDL2.dll", "libz-1.dll", "libepoxy-0.dll"])

    def test_legacy_zlib1_name_is_still_accepted(self):
        (self.prefix / "zlib1.dll").write_bytes(b"zlib")
        exe = self.binary("openwarcraft3.exe", self.runtime_deps("zlib1.dll"))
        dest = self.root / "pkg"
        result = self.run_bundle(dest, exe)
        self.assertBundled(result, dest, ["SDL2.dll", "zlib1.dll", "libepoxy-0.dll"])

    def test_missing_zlib_fails(self):
        exe = self.binary("openwarcraft3.exe", ["SDL2.dll", "libepoxy-0.dll"])
        result = self.run_bundle(self.root / "pkg", exe)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("required zlib1.dll|libz-1.dll not bundled", result.stderr)

    def test_missing_sdl2_fails(self):
        (self.prefix / "libz-1.dll").write_bytes(b"zlib")
        exe = self.binary("openwarcraft3.exe", ["libz-1.dll", "libepoxy-0.dll"])
        result = self.run_bundle(self.root / "pkg", exe)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("required sdl2.dll not bundled", result.stderr)

    def test_unresolved_dependency_fails(self):
        (self.prefix / "libz-1.dll").write_bytes(b"zlib")
        exe = self.binary("openwarcraft3.exe", self.runtime_deps("libz-1.dll") + ["libmissing.dll"])
        result = self.run_bundle(self.root / "pkg", exe)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libmissing.dll => not found", result.stderr)

    def engine_package(self, dest_dir):
        """Engine DLLs already copied into the package, as both callers do."""
        (self.prefix / "libz-1.dll").write_bytes(b"zlib")
        dest_dir.mkdir(parents=True)
        (dest_dir / "libgame.dll").write_bytes(b"engine")
        exe = self.binary("openwarcraft3.exe", self.runtime_deps("libz-1.dll") + ["libgame.dll"])
        game = self.binary("libgame.dll", ["libgcc_s_seh-1.dll", "KERNEL32.dll"])
        return exe, game

    def test_drive_letter_destination_resolves_engine_dlls(self):
        # tools/build-windows.ps1 passes the package dir as "G:/...".
        self.tool("cygpath", FAKE_CYGPATH)
        (self.root / "G:").mkdir()
        (self.root / "g").symlink_to(self.root / "G:")
        exe, game = self.engine_package(self.root / "G:/pkg")
        result = self.run_bundle("G:/pkg", exe, game)
        self.assertBundled(result, self.root / "g/pkg",
                           ["SDL2.dll", "libz-1.dll", "libepoxy-0.dll", "libgcc_s_seh-1.dll"])

    def test_relative_destination_resolves_engine_dlls(self):
        # .github/workflows/release.yml passes the package dir as "release".
        exe, game = self.engine_package(self.root / "release")
        result = self.run_bundle("release", exe, game)
        self.assertBundled(result, self.root / "release",
                           ["SDL2.dll", "libz-1.dll", "libepoxy-0.dll", "libgcc_s_seh-1.dll"])


if __name__ == "__main__":
    unittest.main()
