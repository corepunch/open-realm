# Shared Model Shader Contracts

`renderer/r_shader.c` owns the vertex shader shared by WC3 MDX, WoW M2, and SC2 M3 models. Callers use the same contracts regardless of how many sources their game data provides.

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
