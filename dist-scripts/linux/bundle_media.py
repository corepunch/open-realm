#!/usr/bin/env python3
"""Bundle the Linux FFmpeg runtime closure without replacing the host's libc."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess

REQUIRED = ("libavformat", "libavcodec", "libavutil", "libswscale", "libswresample")
DOC_ROOT = Path("/usr/share/doc")
HOST_RUNTIME = re.compile(r"(?:libc|libm|libdl|libpthread|librt|libresolv|libgcc_s|libstdc\+\+)\.so\.|ld-linux|lib(?:drm|va[.-]|vdpau|GL|EGL|vulkan|OpenCL|X|xcb)")


def dependencies(paths):
    result = subprocess.run(["ldd", *map(str, paths)], check=True, capture_output=True, text=True)
    if "not found" in result.stdout:
        raise RuntimeError("Unresolved media dependencies:\n" + result.stdout)
    return {Path(match.group(1)) for match in re.finditer(r"=> (/[^\n]+?) \(", result.stdout)}


def bundle(binary, destination):
    linked = dependencies([binary])
    roots = [path for path in linked if any(path.name.startswith(name + ".so.") for name in REQUIRED)]
    if not all(any(path.name.startswith(name + ".so.") for path in roots) for name in REQUIRED):
        raise RuntimeError("Linux releases require FFMPEG=1 for background music playback")
    libraries = sorted(path for path in set(roots) | dependencies(roots) if not HOST_RUNTIME.match(path.name))
    notices = {}
    for library in libraries:
        owner = subprocess.run(["dpkg-query", "-S", str(library.resolve())], check=True, capture_output=True, text=True)
        package = owner.stdout.split(": ", 1)[0].split(":", 1)[0]
        notice = DOC_ROOT / package / "copyright"
        if not notice.is_file():
            raise RuntimeError(f"Missing copyright notice for {library}: {notice}")
        notices[package] = notice
    destination.mkdir(parents=True, exist_ok=True)
    for library in libraries:
        output = destination / library.name
        shutil.copyfile(library, output)
        # Executable RUNPATH is not inherited for FFmpeg's transitive libraries.
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(output)], check=True)
    for package, notice in notices.items():
        output = destination.parent / "licenses/ffmpeg" / package / "copyright"
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(notice, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    bundle(args.binary, args.destination)
