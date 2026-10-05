# GL Shader 1.0.0

Standalone C99 shader descriptors, GLSL generation, GL/GLES compilation and
typed uniform submission. Extracted from Corepunch Open Realm. MIT; see LICENSE.
The Open Realm and Orion copies of this directory are byte-identical releases.
Update both copies together and bump `GL_SHADER_VERSION` when the contract changes.
No SDL, window system, engine math types or GL loader is required by this module.

Include your backend's GL declarations before `gl_shader.h` or `gl_shader.c`.
Compile the implementation exactly once. The consumers unity-include it inside
their renderer translation unit. Build dependencies must include this directory.

## Descriptor contract

- `VertexBody` defines `vec4 vert()`; `FragmentBody` defines `vec4 frag()`.
  Neither supplies `#version`, interface declarations or `main()`.
- Uniform offsets address a caller-owned C state struct. `count == 0` means
  scalar; a positive count declares an array, including `[1]`.
- Counted arrays use a `uint32_t` field at `count_offset`; runtime counts must
  be in `1..count`. Compile-time capacity and all CPU offsets are checked
  against `state_size` before compilation. Do not mutate a loaded descriptor.
- Attribute locations are explicit; vertex buffer layout remains the caller's
  responsibility. Varyings are floating-point scalar/vector values. Names and
  shader bodies are trusted source; unsupported shader operations remain the
  compiler's responsibility.
- Descriptor precision metadata is retained for source compatibility with
  Open Realm. ES3 uses matching highp float/int defaults in both stages;
  sampler defaults are lowp. Per-field precision overrides are not emitted.
- Profiles: GLSL 120, 140, 150 and ES 300. Legacy `texture()` maps to
  `texture2D()` for 2D sampling. Rectangle samplers are rejected on ES3 and
  array samplers on GLSL 120. This is not a general shader-language translator.

```c
gs_options_t options = {.dialect = GLSL_DIALECT_ES3};
shaderProg_t program = {0};
if (gs_load(&program, &descriptor, &state, sizeof(state), &options)) {
  gs_apply(&program, &state);
  /* draw using the caller's textures and vertex buffers */
  gs_delete(&program);
}
```

## Lifetime and errors

`gs_load` requires an empty program and preserves non-sampler state. It assigns
sampler units in descriptor order, including sampler arrays. Failed compilation,
linking or allocation returns failure with no partially published program;
all allocated shader objects and storage are released. Diagnostics always go
to stderr and are flushed. The caller chooses whether failure is fatal.

Descriptors, names and bodies must outlive the program. Programs own GL handles,
locations, a uniform cache and conversion scratch space. Optional allocator
hooks must be supplied as a matching pair and outlive the program. Delete each
program with its owning GL context current, before destroying the context.

`gs_apply` binds the program each time, so other renderers changing the current
program cannot invalidate a global binding cache. Every active uniform is
uploaded on first submission. Later submissions compare only the actual field
bytes and array count, excluding struct padding. Bool arrays convert to GLint;
transposed mat3 arrays convert on the CPU and always upload with `GL_FALSE`,
as GLES requires. The draw path allocates no memory.

Uniform writes outside this module require `gs_invalidate`. Program instances
are specific to a GL context; a recreated context requires recreated programs.
The module does not own framebuffer, texture, blending, color-space or context
capability policy.

`gs_link_sources` accepts complete sources using the selected profile's syntax.
It replaces an optional leading `#version` line (after whitespace), prepends
the selected prologue and supplied defines, then compiles and links with the
same error/cleanup contract. Comments before `#version`, legacy syntax rewrites,
and conversions to Metal/Vulkan are outside this API's contract.

## Verification

`test_contract.h` exercises the implementation with a captured GL boundary.
It is included by Orion's `tests/shader_module_test.c` and Open Realm's
`tests/test_gl_shader.c`. It covers profile generation, bounded buffers,
unsupported types, state offsets, compile/link/allocation cleanup, first and
changed uploads, external rebinding, counted-array shrink/grow, bool arrays,
CPU matrix transposition, sampler assignment and raw-source prologues.
Consumers must additionally run their rendering/pixel tests on real drivers.
