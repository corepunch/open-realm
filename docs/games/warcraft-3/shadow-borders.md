# Ground Shadow Borders: Issue 684 Investigation

## Status and visual evidence

Investigation on 2026-11-05 against `6b4a2b4db21c62f971144e59963673c683b09dad`.
[Issue 684](https://github.com/corepunch/open-realm/issues/684) was open with no comments at inspection.
This is an investigation record, **not a verified fix or established retail border contract**.

The issue's [retail capture](https://github.com/user-attachments/assets/e1e50b51-8f61-4727-b016-a134b6b6f7c4)
shows soft transitions around the foreground bare trees and the tower above them.
The [Open Realm capture](https://github.com/user-attachments/assets/322dbcf8-e4cc-48fc-b4aa-9fab1004e1c5)
shows straight rectangular shadow boundaries below the foreground trees, behind the tower, and near the upper-right trees.
These are different camera positions, resolutions, and simulation times; they cannot establish a quantitative opacity difference.
Snow makes the boundary conspicuous, but is not evidence that snow requires different shadow opacity.

## Source trace

| Path | Source and placement | Opacity and composition |
| --- | --- | --- |
| Authored terrain shadow | `w3m/r_war3map.c:R_FileReadShadowMap` reads `(width-1)*4` by `(height-1)*4` bytes from `war3map.shd`; `_W3M_DrawTerrainShadows` submits a whole-map rectangle | Black RGBA, alpha `sample/2`, white alpha 255 vertex multiplier |
| Destructable shadow | `game/g_spawn.c` loads the row's `shadow` via `G_LoadShadowTexture`; sets `shadow_rect=0` | Entity path below |
| Building shadow | `game/g_monster.c:M_SetBuildingShadow` loads `buildingShadowTexture`; sets `shadow_rect=0` | Entity path below |
| Entity splat | `renderer/r_ents.c:R_DrawEntityShadow`; `R_RenderShadow` in WC3 returns false, so the generic decal path owns this draw | Black RGB; texture alpha multiplied by vertex alpha 128 |

Paths without a leading directory above are relative to `games/warcraft-3/`.
`G_LoadShadowTexture` resolves `ReplaceableTextures\\Shadows\\<name>.blp`; optional DDS fallback is enabled for units,
but disabled for the destructable/building calls above. `client/cl_view.c` passes the registered texture and unpacked rectangle
to the renderer. With no explicit rectangle, footprint width and height are texture dimensions times 32 world units;
pivots are rounded `width*0.3` and `height*0.7`. Padding would therefore affect placement as well as filtering if added naively.

Both paths use `sd_shadow_splat` in `renderer/r_shader.c`. `R_SetupSplatState` in
`w3m/r_war3map_ground.c` selects `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, disables depth writes,
and enables polygon offset. The shader uses sampled alpha, not texture RGB or terrain lighting.
Underlying terrain lighting still affects the final blended colour. Fog changes the shadow RGB contribution as described in
[environmental fog](environmental-fog.md#ground-shadow-composition); it does not create a soft spatial border.

## What the padding hypothesis explains

Confirmed from source:

- `R_LoadTextureBLP1Named` uploads every mip at its authored width and height. The decoder traverses all pixels;
  there is no last-row/last-column trim. `R_LoadTextureMipLevel` passes those same dimensions to `glTexImage2D`.
- `R_AddRectSplat` supplies UV bounds `(0,0)..(1,1)`. `R_EmitSplatTile` uses the complete rectangle and flips V;
  it does not skip an existing empty row. Both the cached and uncached paths use this rectangle contract.
- Splats select `GL_CLAMP_TO_EDGE`. Uploads select linear magnification and either linear or trilinear minification.
- Geometry is clipped to the rectangle; the shadow fragment shader also applies a hard `crop_edges` step at UV 0/1.
  No transparent padding is synthesized.

Thus a nonzero alpha at a texture boundary remains nonzero up to the clipped geometry edge, and stops abruptly there.
Linear filtering with `CLAMP_TO_EDGE` cannot interpolate into nonexistent transparent texels.
Existing transparent rows would be retained and sampled. Which source edge appears at the bottom of the world rectangle
must account for the V flip; do not assume image-bottom and world-bottom are the same edge.

Unverified: the affected assets' actual border alpha at each mip; whether retail adds transparent pixels,
uses a transparent sampler border, expands the footprint, or uses another shadow representation. The screenshots alone
cannot distinguish these. The rectangular footprints make entity splats the leading candidate, but no runtime toggle
isolation has yet excluded authored terrain shadows or overlapping contributions.

Do not lower global opacity, zero existing boundary pixels, or add padding until the source asset and footprint contract
have been measured. Do not change shared terrain/selection/decal sampling to compensate for one map.

## Completing the reproduction

1. Use the reported scene (the Human09 campaign scene is suggested by the ship countdown, but the exact map/save
   was not supplied in the issue text). Record archive edition, map/save, camera, time, and resolved tree/building shadow paths.
2. Capture identical frozen scene state with `r_unit_shadows 1` and `0`. This isolates entity splats, **not** the authored
   terrain shadow helper. If needed, temporarily suppress `_W3M_DrawTerrainShadows` independently in a diagnostic build.
   Do not infer source ownership solely from a shadow's proximity to a tree.
3. Inspect decoded alpha on all four sides of every mip of the resolved textures, plus adjacent inner rows/columns.
   Compare the decoded dimensions with BLP metadata and GPU uploads. Check archive overrides and missing-texture logs.
4. Only if boundary alpha explains the cutoff, compare an experimental transparent-border/expanded-footprint implementation
   with retail, preserving texel-to-world scale and pivot. Inspect both outer coverage and overlapping shadow opacity.
5. Capture snow and ordinary grass/dirt with identical lighting and fog, including fog disabled, partial fog, overlapping
   shadows, unit explicit rectangles, and existing transparent-border textures. Preserve interior alpha and normal footprints.

Use bounded engine captures after restoring the scene; the issue attachments are references, not new runtime validation:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +vid_hidden 1 +set r_unit_shadows 1 +map Maps/Campaign/Human09.w3m +screenshot 90 +com_frame_limit 120
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +vid_hidden 1 +set r_unit_shadows 0 +map Maps/Campaign/Human09.w3m +screenshot 90 +com_frame_limit 120
```

The commands alone do not restore the attachment's progressed gameplay state. Confirm the map and reproduce that state
before treating the output as a comparison. Restore `r_unit_shadows 1` afterward.

## Verification and limits

Both original attachments were downloaded and visually inspected. No retail archives or map/save were available in this
environment, so no decoded retail border measurements, engine reproduction, or before/after rendering was possible.
Baseline `make test` failed during compilation with missing `SDL2/SDL.h` and `SDL2/SDL_keycode.h`; no test pass is claimed.
No renderer behavior, opacity, wire format, or save format was changed.

When a correction is established, add a failing regression through the production decoder/upload and splat submission paths
in `tests/test_renderer_model.c` before fixing it. Cover a non-square texture with nonzero outer alpha, authored zero outer rows,
all relevant mips, pivot/32-unit placement, explicit unit rectangles, and unchanged interior opacity. A snow/grass framebuffer
comparison is also required: shader-source assertions alone cannot verify edge appearance. Run `make test`,
`make test-renderer-model`, and `make test-renderer-shadows`; build all games if the shared renderer changes.
