#!/usr/bin/env python3
"""Experimental static Emscripten link using the existing native module boundaries.

This build mounts browser-local archives via WasmFS OPFS; full runtime compatibility is unverified.
"""
import argparse
import pathlib
import os
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def sources(*directories, exclude=()):
    result = []
    for directory in directories:
        result.extend((ROOT / directory).rglob("*.c"))
    return sorted(p for p in result if p.name not in exclude)


def unity(target, files):
    # A quoted include resolves relative to the generated file, NOT the
    # subprocess working directory. Use relative paths from that file.
    includes = (os.path.relpath(path, target.parent).replace(os.sep, "/")
                for path in files)
    target.write_text("/* Generated module unity compilation unit. */\n" +
                      "".join('#include "{}"\n'.format(path) for path in includes),
                      encoding="utf-8")


def run(args):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="build/web/engine")
    parser.add_argument("--emcc", default="emcc")
    parser.add_argument("--check", action="store_true", help="validate/generate sources without invoking Emscripten")
    args = parser.parse_args()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    emcc = shutil.which(args.emcc)
    if emcc is None and not args.check:
        parser.error("emcc not available; activate the Emscripten SDK")

    wc3 = "games/warcraft-3"
    module_sources = {
        "shared": sources("shared") + [ROOT / "vendor/blast/blast.c"],
        "jass": sources(wc3 + "/jass"),
        "renderer": sources("renderer", wc3 + "/renderer"),
        "game": sources(wc3 + "/game", exclude=("g_save.c",)) +
                [ROOT / "port/web/src/web_save_unsupported.c"],
        "menu": sources(wc3 + "/menu"),
        # Compiled once: data/serialization helpers used by both game and menu.
        "wc3_common": sources(wc3 + "/common", exclude=("world_w3.c",)),
        "engine": sources("client", "server", "common", "sound",
                          exclude=("stb_vorbis.c", "sv_routing.c")) +
                  [ROOT / wc3 / "common/world_w3.c", ROOT / "port/web/src/web_opfs.c"],
    }
    # Source collection is deliberately scoped to the known native modules.
    # Never accidentally compile fixture tests or a second platform main().
    for name, files in module_sources.items():
        if not files:
            raise SystemExit("empty browser module: " + name)
        missing = [str(path) for path in files if not path.is_file()]
        if missing:
            raise SystemExit("missing inputs for " + name + ": " + ", ".join(missing))
    # Compile each native unity module separately, preserving module-local static
    # definitions and the direct GetGameAPI/R_GetAPI/M_GetAPI interfaces.
    flags = ["-O2", "-I.", "-Ishared", "-Ishared/types", "-I" + wc3,
             "-I" + wc3 + "/common", "-I" + wc3 + "/game",
             "-I" + wc3 + "/game/api", "-I" + wc3 + "/game/skills",
             "-DWC3", "-DUSE_FOGOFWAR", '-DBZ_GAME="warcraft-3"',
             "-DBZ_GL_ES3", "-DBZ_MSAA_SAMPLES=0",
             "-sUSE_SDL=2", "-sUSE_ZLIB=1", "-pthread"]
    objects = []
    for name, files in module_sources.items():
        unit = out / (name + "_unity.c")
        unity(unit, sorted(set(files)))
        obj = out / (name + ".o")
        # The WC3 game and menu are separate native libraries with independent
        # FDF parser state/callbacks. The static WASM link must preserve both
        # instances under distinct symbol namespaces. Only the engine has its
        # own unrelated FDF implementation.
        extra = ["-DSTB_FDF_IMPLEMENTATION", "-DSTB_FDF_GLOBALS"] if name in ("game", "menu", "engine") else []
        if name == "engine":
            extra.append("-DBZ_CLIENT_WORLD")
        if name in ("game", "menu"):
            # Each native module owns a distinct FDF registry/parser and host
            # callbacks. Preserve that separation in the single WASM link.
            extra.extend(["-include", "port/web/src/web_fdf_namespace.h",
                          "-DBZ_WEB_FDF_" + name.upper()])
        if not args.check:
            run([emcc, *flags, *extra, "-c", unit, "-o", obj])
        objects.append(obj)
    for name in ("parser", "sheet"):
        obj = out / (name + ".o")
        if not (ROOT / wc3 / "sheet" / (name + ".c")).is_file():
            raise SystemExit("missing sheet source: " + name)
        if not args.check:
            run([emcc, *flags, "-c", ROOT / wc3 / "sheet" / (name + ".c"), "-o", obj])
        objects.append(obj)
    # The existing renderer embeds generated font bytes from this source file.
    if not (ROOT / "renderer/conchars_sysfont.h").exists():
        raise SystemExit("renderer/conchars_sysfont.h missing; run native `make font` first")
    if args.check:
        print("Source validation complete; generated unity files in", out)
        return
    shell = ROOT / "port/web/site/engine-shell.html"
    run([emcc, *objects, "-O2", "-sUSE_SDL=2", "-sUSE_ZLIB=1",
         "-sMIN_WEBGL_VERSION=2", "-sMAX_WEBGL_VERSION=2",
         "-sWASMFS=1", "-pthread", "-sPROXY_TO_PTHREAD=1", "-sPTHREAD_POOL_SIZE=2",
         "-sALLOW_MEMORY_GROWTH=1", "-sFILESYSTEM=1", "-sENVIRONMENT=web",
         "-sASSERTIONS=1", "-sEXPORTED_RUNTIME_METHODS=addRunDependency,removeRunDependency", "-lm", "--shell-file", shell,
         "-o", out / "index.html"])
    for name in ("installer.js", "install-worker.js", "filesystem-bridge.js"):
        shutil.copy2(ROOT / "port/web/site" / name, out / name)
    print("Experimental static engine build emitted to", out)
    print("OPFS archives mounted through WasmFS; native engine link/runtime remains experimental.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
