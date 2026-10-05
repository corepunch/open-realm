# Shared GL shader module

Open Realm and Orion UI vendor byte-identical GL Shader releases under
`vendor/gl_shader/`. The [module contract](../../vendor/gl_shader/README.md)
describes GLSL generation, typed storage, profile limits, allocators and release
synchronization. No game, SDL or engine math types are part of that module.

`renderer/r_shader.c` compiles the implementation once. `renderer/shader_desc.h`
keeps the public `R_LoadShader` / `R_ApplyShader` API and field-description macros.
`R_LoadShader` now supplies `sizeof(shader.state)` so the shared loader validates
all offsets and fixed array capacities. Existing game descriptors and GLSL
bodies stay renderer-owned. Profiles still follow `GL_BACKEND` and `GLSL`.

The reusable functions return failure and clean up rejected programs. Open Realm's
wrapper deliberately exits on a built-in failure: `ri.error` is a logger and
cannot guarantee termination. No failed program is cached or drawn. Storage
uses the module's matching malloc/free pair; optional allocator hooks allow
other consumers to choose their allocation policy.

Every active uniform uploads on the first draw. Later submissions compare exact
field bytes and active array counts. Shrinking and growing an array both trigger
an upload. Matrix transposition happens in CPU scratch storage; GLES uploads
always use `GL_FALSE`. A submission always binds its own program, even after an
external `glUseProgram`, with no global cached binding across GL contexts.
Program-owned storage is released before destroying the owning context.

`R_BuildShaderDeclarations` and `R_BuildShaderMain` retain public inspection
entry points and return -1 for invalid profiles/interfaces or undersized buffers.
Compilation uses a sizing pass, so declarations are not limited to a fixed
2048-byte stack buffer. Uniform `count == 1` now correctly declares `[1]`.

```sh
make -j4 test-gl-shader test-renderer-model test-renderer-shadows
make -j4 test openwarcraft3 openwow opensc2
python3 tools/engine_boundary_audit.py --base origin/main
```

The shared contract tests capture the real implementation's GL calls, including
failure cleanup, storage rejection, cache hits/misses, sampler arrays and GLES
matrix semantics. Existing renderer suites cover actual game descriptors and
retain fatal-error assertions. These tests don't prove all shader bodies work
on a GLES driver; backend shader-resource limits still require driver validation.

See also [model shaders](model-shader.md), [vendored dependencies](../vendored-dependencies.md),
and [renderer platforms](../build-and-renderer-platforms.md).
