# Shared Model Shader Contracts

`renderer/r_shader.c` owns the vertex shader shared by WC3 MDX, WoW M2, and SC2 M3 models. Callers use the same contracts regardless of how many sources their game data provides.

## Lighting

`uLightCount` is always in `[1, 8]`. There is no zero-light fallback and no parallel directional-light uniforms. A title with one world sun packs it into `uLights[0]`; WC3 models with embedded sources upload those sources and fold scene ambient into the first entry.

Each `uLights[i]` mat4 stores one source by GLSL column:

| Column | Values |
|---|---|
| `0` | world position XYZ, type (`0` omni, `1` directional, `2` ambient) |
| `1` | direction XYZ, attenuation start |
| `2` | diffuse RGB, diffuse intensity |
| `3` | ambient RGB, ambient intensity |

`R_PackDirectLight` accepts a direction from the surface toward the light and negates it for the stored source-direction convention used by the shader.

## Instanced grass

The instanced model shader receives the complete effect state in `uGrassParams`, not seven independent uniforms. Its columns are:

| Column | Values |
|---|---|
| `0` | camera XY, fade start, fade end |
| `1` | elapsed seconds, wind speed, wind amplitude, root fraction |
| `2` | phase X/Y, sway direction X/Y |
| `3` | model Z min/max, enabled (`0` or `1`), reserved (`0`) |

`R_PackModelGrass` is the CPU-side schema. Camera Z is deliberately absent because distance fade is evaluated in world XY.

## Extension rule

Do not add a special count value, a second uniform family, or a per-game shader branch when an existing entry can encode the state. Extend the common schema only when authoritative data requires another value. If the common representation is genuinely incapable of expressing a title's behavior, document the exact constraint before adding an exception.
