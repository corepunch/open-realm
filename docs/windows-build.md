# Native Windows build

OpenWarcraft3 uses GNU Make and a GCC-style unity build. On Windows, use an
MSYS2 UCRT64 environment with these packages:

```sh
pacman -S --needed make \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-zlib \
  mingw-w64-ucrt-x86_64-libepoxy
```

From PowerShell, `tools/build-windows.ps1` builds the native executable and
writes a runnable folder to `dist\windows` by default. Override `-MsysRoot`
and `-PackageDir` when MSYS2 or the package belongs in a different location.

## What a runnable Windows folder must contain

A clean Windows install has no MSYS2, and Windows resolves DLLs from the
executable directory, so the package must ship every third-party DLL beside
`openwarcraft3.exe`:

- the engine build outputs (`openwarcraft3.exe`, `build-windows/lib/*.dll`),
- the staged `build/share` tree (engine fonts plus per-game defaults),
- the full MinGW runtime closure: SDL2, zlib, libepoxy **plus** their
  transitive deps (e.g. `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`).
  Copying only the three named DLLs is not enough; without the transitive
  ones the exe still fails to start.

Both packaging paths compute that closure with `ldd` instead of a hardcoded
list, so new third-party links are picked up automatically:

- `dist-scripts/windows/bundle_mingw_dlls.sh <dest> <binaries...>` — copies
  every `ldd`-resolved DLL under the MSYS2 environment prefix into `<dest>`,
  ignores system DLLs (`C:/Windows/...`), and fails loudly when a dependency
  is unresolved (`not found`) or a required DLL (`SDL2.dll`, `zlib1.dll`,
  `libepoxy-0.dll`) is absent. That failure is the regression guard for
  issue #462: the release job breaks instead of shipping a zip that cannot
  start.
- `tools/build-windows.ps1` calls that script for local packages;
  `.github/workflows/release.yml` calls it for `openwarcraft3-windows-x64.zip`
  (MINGW64 runner packages `mingw-w64-x86_64-zlib` explicitly since the
  engine links `-lz`).

The Windows renderer uses libepoxy for runtime OpenGL dispatch because the
system `opengl32.dll` directly exports only OpenGL 1.1. Networking uses Winsock
2 and retains the same loopback/UDP behavior as the POSIX implementation.
