# Contributing

## Test Fixtures and MPQ Assets

- Tests must not depend on a developer's local Warcraft III data or `War3.mpq`. Add any archive fixtures under `tests/resources-src`, pack them into the generated `build/tests/tests.mpq` through `make test-assets`, and point tests at that fixture MPQ instead.
- Tests must not read from ignored local extraction folders such as `data/fdf` or `data/Warcraft III`. If a test needs FDF, map, texture, model, or other archive content, copy the minimal fixture into `tests/resources-src`, add it to `build/tests/tests.mpq`, and read it from that generated archive.
- When a test fixture intentionally replaces an actual game archive file with custom content, use the same archive path and filename as the real game file. Do not invent project-specific replacement names for files that are meant to stand in for game files; keep the name WoW/Warcraft-style and make only the contents custom.

## Build and Linking

- Never add `DYLIB_LOOKUP := -Wl,-undefined,dynamic_lookup` or otherwise rely on `-Wl,-undefined,dynamic_lookup` in this repository.
- If a target has unresolved symbols, fix the dependency graph or shared implementation instead of weakening the linker contract.
