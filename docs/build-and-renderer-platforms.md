# Build And Renderer Platforms

## Build Profiles

The Makefile has three independent build choices:

| Variable | Values | Default | Contract |
|---|---|---|---|
| `BUILD` | `debug`, `release` | `debug` | `release` adds `-O2`; debug adds `-O0 -g` |
| `GL_BACKEND` | `gl`, `gles3` | `gl` | Desktop OpenGL 3.1, or Linux OpenGL ES 3.0 |
| `MSAA` | `0`, `2`, `4`, `8` | `0` | Compile-time default framebuffer sample count |

All configurations currently share `build/`, so clean before changing any build variable:

```bash
make clean
make BUILD=release GL_BACKEND=gles3 MSAA=0 openwarcraft3
```

That is the recommended RG40XX build. Its Mali-G31 hardware supports OpenGL ES 3.2, while Mesa Panfrost exposes OpenGL ES 3.1 on G31. The runtime only requests ES 3.0. Confirm the firmware driver rather than assuming it from the GPU:

```text
GL_RENDERER: Mali-G31 (Panfrost)
GL_VERSION: OpenGL ES 3.x ...
```

`llvmpipe` identifies software rendering. The GLES3 build is Linux-only; macOS, Windows, and OpenBSD retain desktop OpenGL.
Release keeps assertions enabled: the JASS VM uses them for invariant diagnostics, while optimization is controlled independently.

The first release profile used `-DNDEBUG` and exposed VM expressions inside `assert(...)`: indexed JASS assignments skipped both
their index and value evaluation, then `jass_copy` read an unrelated stack entry during `G_ClientBegin`. Keep VM operations outside
assertions. Diagnose this class with a bounded campaign launch; a crash report ending in
`jass_copy -> jass_set_array_value -> jass_resumecoroutine` identifies the old failure.

For an optimized desktop build with 4x coverage:

```bash
make clean
make BUILD=release MSAA=4 openwarcraft3
```

## JASS Header Dependencies

WC3's `libjass` directly includes game structs through `jass.h -> game/g_local.h`. Header changes must rebuild it even
when no JASS source changes. `games/warcraft-3/game.mk:JASS_HEADERS` covers game/common/server/shared/client headers;
`make test-jass-build` verifies the dependency using pretend-new header timestamps. A stale VM after the entity-state
trim caused ESC to move cutscene units but skip local-player camera/UI cleanup; see
[WC3 cinematics](games/warcraft-3/cinematics.md#debugging) for the reproduction and regression tests.

## Texture Channel Order

`COLOR32` is four **RGBA bytes** in engine code; BLP decoders retain their source BGRA order. The uploader receives a
`TEXMIP` with an explicit `PIXEL_RGBA` or `PIXEL_BGRA` format. That describes memory, not the OS or GPU's storage.
`R_InitTextureFormats()` runs once per GL context before loading textures; `r_bgra_internal` records the supported
BGRA internal format (zero means BGRA must be converted). Every upload still uses `GL_UNSIGNED_BYTE`.

| Context | BGRA internal / external format | RGBA source |
|---|---|---|
| Desktop GL, including a desktop API exposed by gl4es | RGBA / BGRA, original buffer | RGBA / RGBA, original buffer |
| GLES with `GL_EXT_texture_format_BGRA8888` | BGRA_EXT / BGRA_EXT, original buffer | RGBA / RGBA, original buffer |
| GLES with `GL_APPLE_texture_format_BGRA8888` | RGBA / BGRA_EXT, original buffer | RGBA / RGBA, original buffer |
| GLES without either extension | Copy BGRA to RGBA, upload RGBA / RGBA, free copy | RGBA / RGBA, original buffer |

EXT takes precedence when both extensions exist. The capability is derived from `GL_VERSION` and advertised extensions,
not `__linux__`, GPU vendor, or CPU endian. The startup log prints `texture uploads RGBA=direct BGRA=direct` or
`BGRA=CPU conversion to RGBA`, plus the selected internal format. Unsupported BGRA conversion preserves the caller's
buffer. Null-data storage allocation needs no conversion. See the distinct format-pair requirements in the
[EXT specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_format_BGRA8888.txt) and
[APPLE specification](https://registry.khronos.org/OpenGL/extensions/APPLE/APPLE_texture_format_BGRA8888.txt).

| Source | Format passed to uploader |
|---|---|
| BLP1 JPEG/palette; BLP2 palette/raw/DXT | BGRA; retain decoder output and upload directly when supported |
| STB PNG/TGA/JPEG | RGBA; no intermediate BGRA copy |
| PCX | RGBA; read palette RGB components into matching fields |
| Generated pixels / SC2 terrain masks | RGBA; never compensate for platform in generators |
| 32-bit uncompressed DDS | RGBA or BGRA from DDS channel masks; same common upload policy |
| Compressed / 24-bit DDS | Existing separate compressed / RGB/BGR upload paths |

The old uploader (Linux branch introduced in `572cfcc73`) interpreted all loaded BGRA image bytes as RGBA on Linux,
but as BGRA elsewhere. M1 campaign logging captured BGRA upload bytes such as `(3,3,255,255)`. The OS conditional
caused different colors from identical buffers. Merely changing its GL enum breaks RGBA-generated pixels; the source
format must be explicit, and SC2's compensating desktop mask swizzle must be removed together.

Independent corroboration: [open-realm b56a8618](https://github.com/sookyboo/open-realm/commit/b56a86180174943cc0b4132fb6b8d3d132b2fc46)
fixes the same gl4es/PortMaster mismatch using separate RGBA/BGRA wrappers and a compile-time GLES conversion path.
This tree uses one source-format-aware uploader and runtime capability detection, including GLES BGRA extensions.
An earlier local revision normalized every BLP to RGBA; it has been superseded to avoid conversions where BGRA works.

Reference checkout: `data/ioquake3`, upstream `ioquake/ioq3`, inspected commit
`588393618dbc82e7207c21c6ddecca229944a03a`. `code/renderercommon/tr_image_tga.c:R_LoadTGA` converts source BGR into RGBA;
`code/renderergl1/tr_image.c:Upload32` uploads RGBA without OS-dependent channel selection. Both designs keep the source
bytes consistent with the API format; they differ in when conversion is needed.

Verification: `make test-renderer-model` intercepts GL uploads. It emulates desktop GL, GLES EXT, GLES APPLE and plain
GLES contexts, checks the exact internal/external pair, asymmetric R/B/alpha values, zero allocation for direct uploads,
one allocation/free for converted BGRA, unchanged input memory, and no extension queries during uploads. BLP1 palette,
BLP2 raw/palette/DXT, PCX and 32-bit DDS tests cover loader format declarations. `make test` includes this suite.
Run bounded ROC/TFT, WoW and SC2 scenes after changes (see [scene workflow](rendering-scene-workflow.md)).

For platform reports, collect OS, build flags (`GL_BACKEND`), startup `GL_VENDOR`/`GL_RENDERER`/`GL_VERSION`, the texture
upload capability log, and an engine `+screenshot 10 +com_frame_limit 20` of the same scene. Ask whether textures,
solid UI/vertex colors, or only screenshots are inverted. Do not introduce a global driver/OS swizzle toggle.
Mac runtime checks and emulated capability tests do not substitute for Windows/Linux hardware validation.

2026-08-26 verification: `make test` passed 5,250 assertions in 772 tests. All three games built, and bounded Human02
ROC/TFT (including ESC), WoW character creation, and SC2 TRaynor01 runs completed on Apple M1. The capability log showed
both source formats uploading directly; engine screenshots retained correct colors and WC3 gameplay camera/HUD cleanup.
SC2 still reported missing DDS assets and an unloadable-texture warning, so its launch is a smoke check, not full asset QA.

## API Floor

The supported renderer floors are desktop OpenGL 3.1 and OpenGL ES 3.0. GLES3 shaders are generated from the same GLSL 1.40 bodies with a `#version 300 es` header and precision declarations.

OpenGL 2.0 and OpenGL ES 2.0 are intentionally unsupported. The renderer relies on vertex-array objects, instanced drawing, vertex attribute divisors, and GLSL `in`/`out` syntax. More importantly, the shared MDX/M2/M3 skinning shader needs a sizeable matrix palette; ES2 guarantees too little vertex-uniform storage for the current renderer contract. Supporting ES2 would require a separate renderer design rather than version-string substitutions.

At initialization the renderer reads the available vertex-uniform vectors, reserves space for view, lighting, and grass uniforms, and compiles `uBones` to the remaining matrix count up to 128. If less than 128 fits, startup logs:

```text
OpenGL: only N/128 bone matrices fit; complex models may animate incorrectly
```

Bone indices are clamped to the allocated shader palette. This keeps rendering defined, but models that genuinely require the missing matrices can deform incorrectly; the warning must not be suppressed.

## MSAA And Alpha-Key Materials

The build selects the alpha-key fragment shader at C compile time:

- An MSAA build defines `BZ_USE_MSAA`, converts the alpha edge with `fwidth`/`smoothstep`, enables `GL_SAMPLE_ALPHA_TO_COVERAGE`, and retains depth writes.
- An `MSAA=0` build compiles a hard alpha test (`discard` below `uAlphaCutoff`) and retains depth writes. It has no multisample bandwidth cost.

MSAA is fixed at build time because the sample count is part of SDL context creation. There is no `r_msaa` cvar. Startup logs requested and active samples; `GL_SAMPLE_BUFFERS` is authoritative.

## Video Modes

`vid_mode` is a resolution-table index, not a window-style enum. The authoritative table is `common/video_modes.h` and is shared by client startup and the Warcraft III options menu. Mode 0 is 640x480 and is the safe default for the RG40XX display. Invalid indices resolve to mode 0 and the options menu applies changes through `vid_apply`.

Use this bounded diagnostic after changing video or renderer setup:

```bash
build/bin/openwarcraft3 -data 'data/Warcraft III' +set vid_mode 0 +set r_stats 1 +com_frame_limit 100
```

Check `Drawable size`, `GL_RENDERER`, `GL_VERSION`, `MSAA`, `Bone palette`, and `[R_STATS]` in the log. SDL reports physical drawable pixels, so a 640x480 logical window is 1280x960 on a 2x Retina display; the RG40XX panel has no such high-DPI multiplier.

## Authoritative References

- [Arm Mali-G31 product support](https://support.arm.com/compute-ip/mali-g31)
- [Mesa Panfrost hardware/API table](https://docs.mesa3d.org/drivers/panfrost.html)
- [Anbernic RG40XX H specifications](https://anbernic.com/en-ca/products/rg40xx-h)
