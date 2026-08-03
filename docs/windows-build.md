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
copies the engine DLLs plus SDL2, zlib, and libepoxy into one runnable folder.
It uses the standard `C:\msys64` installation and writes to `dist\windows` by
default. Override `-MsysRoot` and `-PackageDir` when MSYS2 or the package belongs
in a different location.

The Windows renderer uses libepoxy for runtime OpenGL dispatch because the
system `opengl32.dll` directly exports only OpenGL 1.1. Networking uses Winsock
2 and retains the same loopback/UDP behavior as the POSIX implementation.
