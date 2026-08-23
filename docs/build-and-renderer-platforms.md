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
