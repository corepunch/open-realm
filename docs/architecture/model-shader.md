# Shared Model Shader Contracts

`renderer/r_shader.c` owns the vertex shader shared by WC3 MDX, WoW M2, and SC2 M3 models. Callers use the same contracts regardless of how many sources their game data provides.

## Bone palette

`BZ_BONE_PALETTE_MAX` is 128 matrices, matching the literal `uBones[128]` before commit `2629f076` (#160).
The C preprocessor stringifies this constant into literal `uniform mat4 uBones[128];` for both ordinary and instanced shaders.
CPU palette storage/uploads use the same constant (MDX's `MDX_MATRIX_PALETTE` aliases it). There is no runtime `tr.bone_count`,
GLSL `BZ_BONE_COUNT` define, capacity query, or sizing helper. A C macro expanding to 128 is equivalent to the old literal;
keeping one constant prevents CPU storage and shader array sizes from drifting apart. Vertex palette indices are
`int(i_skin1[i]) + int(uFirstBoneLookupIndex)` and must not be clamped to a hardware-derived estimate.

### Regression and correction

`2629f076` made `tr.bone_count` depend on `R_BonePaletteSize` and added a shader clamp. This changed both shader array length
and CPU upload counts. WC3 `MDLX_BindGeosetMatrixPalette` also stopped constructing palette entries above the reduced count.
At 64 entries, an asset index of 83 became 63: unrelated vertices used the last matrix, corrupting animation and potentially
the apparent size/position of portraits. A model's total skeleton count is not its draw's palette size: MDX geosets map palette
slots through `matrixPalette[]` to node matrices.

Imported [sookyboo's correction](https://github.com/sookyboo/open-realm/commit/069bdced50a477fe53458e1b4b3c398c669db3ba)
initially retained the estimate only as a warning and restored the fixed palette and unchanged indices. The subsequent
hardening removed the misleading estimate and mutable count entirely; shader compile/link results determine backend support.

### Uniform units and the user's report

`GL_MAX_VERTEX_UNIFORM_COMPONENTS` counts scalar components; divide by four to get vec4 vectors. One mat4 consumes four vec4s
(16 components). The removed `BZ_BONE_UNIFORM_RESERVE = 64` meant **64 vec4s reserved for other uniforms**, not 64 available bones.
The former diagnostic estimate was `max(1, min(128, floor((vectors - 64) / 4)))`, with values below the reserve returning 1.
Examples: 256 vectors gives 48 matrices, 320 gives 64, and 576 reaches 128. This budget was a renderer estimate, not a direct
hardware bone-count query. The GLSL compiler/linker checks actual shader resources.

The linked [gist](https://gist.github.com/sookyboo/737f7aec1f7be2b34f5fd1b22c0050f1) is an agent's explanation, **not a driver log**;
its 64-entry example is hypothetical. It cannot establish the user's actual query result or backend capacity.

There is a concrete gl4es query hazard: at upstream revision `81547d986798e876de8b434193920b606a72363f`,
[`gl4es_glGetIntegerv`](https://github.com/ptitSeb/gl4es/blob/81547d986798e876de8b434193920b606a72363f/src/gl/getter.c)
has no translation for desktop `GL_MAX_VERTEX_UNIFORM_COMPONENTS` (`0x8B4A`); it forwards the enum to GLES. GLES2 supports
`GL_MAX_VERTEX_UNIFORM_VECTORS` (`0x8DFB`), not that desktop enum. An invalid query leaves the initialized result at zero,
which the old sizing helper turns into **one matrix**. This is a source-confirmed risk in that gl4es revision, not a confirmed
diagnosis of the user's installed build. Obtain its version, raw query value and immediate `glGetError()` before claiming it.
Do not infer capacity from the physical GPU name. Production no longer issues this unnecessary query.

### Verification and limitations

Temporary targeted logs on an Apple M1 Pro / OpenGL 4.1 context showed 1024 uniform vectors, a linked model program with
128 active `uBones` entries, and ROC menu geoset palettes of 84, 91 and 124 entries. Temporarily substituting a 320-vector
report reproduced a linked 64-entry shader, 64-matrix uploads for those same geosets, and index 83 mapping to 63. This proves
the truncation mechanism locally; it does not reproduce the user's GLES/gl4es driver. Diagnostic edits were removed.

`tests/test_renderer_model.c` captures the actual shader-source submission for ordinary and instanced variants, asserting
literal 128-entry storage and direct indices without renderer initialization or a display. Its GL mocks exercise compile/link
success, vertex/fragment/link rejection, missing/unallocatable driver logs, both shader caches, and the full one-time instanced
identity palette upload. Failed stages must terminate before using or caching a program. Run `make test`.
For live verification, build all three games and launch bounded scenes (WC3 must cover both archive variants):

```bash
make -j4 openwarcraft3 openwow opensc2 install-share
build/bin/openwarcraft3 -data 'data/Warcraft III' +menu_main +screenshot 10 +com_frame_limit 20
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +menu_main +com_frame_limit 20
build/bin/openwow -data data/world-of-warcraft +menu_character_create +com_frame_limit 20
build/bin/opensc2 -data data/StarCraft2 +map TRaynor01 +com_frame_limit 20
```

Fixed 128 entries cannot overcome a real uniform-storage limit. Supporting such devices needs a separate renderer design
(e.g. palette batches with remapped vertices or another matrix transport). Do not claim the imported patch implements that.
`R_CheckShader` now checks both `GL_COMPILE_STATUS` and `GL_LINK_STATUS`, prints the full driver log (or an explicit
missing-log/allocation diagnostic), and exits with `EXIT_FAILURE`. This is intentional: `ri.error` is wired to `CON_printf`,
and even `Com_Error` currently only prints, so neither guarantees termination. Do not replace this with either logger and
continue drawing. `R_ModelShader` no longer substitutes `SHADER_DEFAULT`, which cannot skin model vertices. Successful links
mark the attached shader objects for deletion; the linked program retains them for its lifetime.

The hardening investigation used a bounded ROC launch with a temporary GL probe: both model shader stages compiled, but
requesting a nonexistent transform-feedback output produced `GL_LINK_STATUS = 0` and a driver error; the ordinary model
program linked successfully. This confirms why compilation status alone was insufficient. The probe was removed.

See also [renderer platforms](../build-and-renderer-platforms.md) and
[Khronos uniform resource rules](https://wikis.khronos.org/opengl/GLSL_Uniforms).

## Lighting

`uLightCount` is always in `[1, 8]`. There is no zero-light fallback and no parallel directional-light uniform family. Game renderers populate one semantic `MODELLIGHTING` value and call `R_SetModelLighting` once; only that renderer proxy packs and uploads `uLightCount` and `uLights[]`.

WoW supplies its world sun, WC3 supplies embedded sources or its default sun, and SC2 supplies the complete three-source key/fill/back rig. Scene ambient is part of `MODELLIGHTING` and the proxy folds it into the first packed entry exactly once.

Each `uLights[i]` mat4 stores one source by GLSL column:

| Column | Values |
|---|---|
| `0` | world position XYZ, type (`0` omni, `1` directional, `2` ambient) |
| `1` | direction XYZ, attenuation start |
| `2` | diffuse RGB, diffuse intensity |
| `3` | ambient RGB, ambient intensity |

`RMODELLIGHT.dir` points from the surface toward the light. The proxy negates it for the stored source-direction convention used by the shader. Game code must not access the shader uniform locations or packed mat4 schema.

## Instanced grass

The instanced model shader receives the complete effect state in `uGrassParams`, not seven independent uniforms. Its columns are:

| Column | Values |
|---|---|
| `0` | camera XY, fade start, fade end |
| `1` | elapsed seconds, wind speed, wind amplitude, root fraction |
| `2` | phase X/Y, sway direction X/Y |
| `3` | model Z min/max, enabled (`0` or `1`), reserved (`0`) |

`R_SetModelGrass` is the upload boundary and `R_PackModelGrass` is its CPU-side schema helper. Camera Z is deliberately absent because distance fade is evaluated in world XY.

The instance transform is declared as one `in mat4 i_instance`. OpenGL assigns its four columns to consecutive attribute locations beginning at `attrib_instance`; buffer setup still describes those four columns because the vertex API operates per location.

## Extension rule

Do not add a special count value, a second uniform family, or a per-game shader branch when an existing entry can encode the state. Extend the common schema only when authoritative data requires another value. If the common representation is genuinely incapable of expressing a title's behavior, document the exact constraint before adding an exception.
