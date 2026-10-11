# Warcraft III main-menu rendering

The main-menu background is an authored MDX camera scene. `menu_glue_scene.c` resolves
`GlueSpriteLayerBackground` through the edition's skin, retains a camera instance and particle
clock, and submits `RDF_USE_ENTITY_CAMERA | RDF_NOWORLDMODEL | RDF_ISOLATED_PARTICLES`.
The renderer draws solid geometry, transparent geometry, then the isolated particles. The
foreground panel sprites are separate draws. Do not recreate background effects procedurally.

## Issues 620 / 621: regression versus missing support

Investigation of [TFT #620](https://github.com/corepunch/open-realm/issues/620) and
[RoC #621](https://github.com/corepunch/open-realm/issues/621) found two distinct causes:

| Symptom | Cause and history |
|---|---|
| RoC clouds, foreground grass and water overlay; TFT tower glow and lens flare | Regression in `f99103bb806faf25c992e6678d79f7e7068c6b72` (2026-10-08, “wc3: render alpha units over water”). MDX now defers blended layers to `RENDER_PHASE_ALPHA`, but the entity-camera shortcut in `R_RenderFrame` only called `R_DrawEntities`. |
| TFT distance haze, muted background contrast | Missing menu support. The glue background never read `[MenuZFog]` and submitted no enabled distance fog. This was also absent at the meteor-fix revision. |

Sookyboo's meteor commit on the current history is
`d681a61015d74cb9705f40927b16605e4a6aad38` (2026-09-30). The equivalent earlier branch commit is
`57d78d60aac569c2f2165d9688ec4992b366c91c`, dated 2026-09-30.
It retained `EVTS` / `SPN` children across frames and drew them in their source camera.
At that revision MDX drew both opaque and blended geosets within one model submission,
so the later missing-alpha-pass regression did not apply. That code evidence does **not**
establish frame-for-frame retail parity, particularly for the then-missing menu fog.

Stock local archive evidence:

- RoC `War3.mpq`, `UI/Glues/MainMenu/MainMenu3d/MainMenu3d.mdx`: material 3 and
  27 use `Textures/cloudstile2.blp` with MDX filter 4; materials 6 and 7 use
  `GrassTuft.blp` with filter 2. These are geoset material layers, not missing map grass.
- TFT `Frozen Throne/War3x.mpq`, `UI/Glues/MainMenu/MainMenu3D_exp/MainMenu3D_exp.mdx`:
  material 2 uses `Textures/Blue_Glow2.blp` and material 4 uses
  `Textures/lensflare1Ax.blp`, both with filter 3. The authored art already exists.
- Both backgrounds contain `PRE2`, not `PREM`; unimplemented ParticleEmitter1 runtime
  support is not the explanation for these reported missing layers.

## Render ownership

`R_RenderFrame` explicitly establishes `RENDER_PHASE_SOLID` for the camera's retained
event children and source entities, then calls `R_DrawAlphaSurfaces` under
`RENDER_PHASE_ALPHA`, before drawing particles. It restores the caller's render phase
and borrowed view afterward. This also protects cameras entered while a prior draw
left a different phase active.

WC3 `R_DrawAlphaSurfaces` draws source MDX alpha layers and retained event-child alpha
layers belonging to that camera's model **and instance ID**. World water, weather and
lightning are excluded from no-world scenes. The world alpha pass also draws world
event children. Child event dispatch runs only in the solid pass; the second draw
must not fire nested sounds/events again or emit a second set of particles.

## MenuZFog data

The glue scene reads the mounted `UI/MiscData.txt` once per scene lifetime. Resetting
the scene on edition changes reloads the values. This is menu-owned presentation;
gameplay's `CS_SCENE_FOG` / `DefaultZFog` state is independent.

| Field | RoC archive | TFT archive cells (RoC, TFT) |
|---|---|---|
| Style | `0` | `0,0` |
| Start | `100` | `100,0` |
| End | `5500` | `5500,7000` |
| Density | `1` | `1,1` |
| Color (ARGB bytes) | `255,11,20,11` | `255,11,20,11,255,178,178,204` |

Scalar cell selection uses `fs_expansion != 0`; color selects a four-cell group.
Singleton values fall back to cell zero (or the first ARGB group) even under TFT.
The stock style is linear (`0`); the current menu path supports that style, honors
disabled style `-1`, and requires finite values with `End > Start`. Density and
color alpha are not needed by the existing linear RGB fog shader. Other fog styles
remain unsupported. Additive glow/flare layers retain their material fog policy;
do not tint every transparent quad toward the fog color.

## Verification and diagnostics

Tests use fixture archives, not an installed Warcraft data directory:

- `renderer_view.entity_camera_draws_transparent_layers_before_particles`: production
  camera entry point, explicit solid/alpha order, particle order and caller-phase restoration.
- `renderer_model.camera_alpha_pass_draws_owned_event_children_without_world_water`:
  production alpha pass, retained child draw, camera identity isolation, unchanged event clock.
- `menu_fdf.glue_background_uses_edition_menu_zfog`: real menu drawing with non-stock
  versioned fixture values under both editions.
- `menu_fdf.expansion_menu_zfog_accepts_singleton_and_disabled_rows`: singleton fallback
  and disabled fog through the same menu entry point.

The missing-pass and versioned-fog tests failed before their fixes. The retained-child
test also fails if its alpha-pass traversal is removed.

```sh
make test-renderer-view test-renderer-model test-renderer-shadows test-menu
make openwarcraft3
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat UI/MiscData.txt
build/bin/mpqtool -mpq 'data/Warcraft III/Frozen Throne/War3x.mpq' cat UI/MiscData.txt
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +vid_hidden 1 +screenshot 100 +com_frame_limit 600
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +vid_hidden 1 +screenshot 100 +com_frame_limit 600
```

Framebuffer composition needs a display/OpenGL context and remains outside the headless
tests. Local 2048×1536 captures `screenshots/shot0205.jpg` (RoC) and `shot0206.jpg` (TFT)
show the restored layers, with the TFT haze/flare/glow comparable to the issue's retail
reference. Animation phases and asset versions differ; these are not pixel-identical goldens.
An unrelated edition-button texture is missing from these older local archives.

Validation: baseline and final `make test` passed with local UDP socket access.
Forced debug and release builds of `openwarcraft3`, `openwow`, and `opensc2` passed,
as did renderer tests with and without `USE_SHADOWMAPS`, the menu-boundary audit,
the engine-boundary audit, and `git diff --check`. Network and save formats are unchanged.

If startup crashes at `FS_LoadSheetTyped` from `S_LoadUISoundsSlk` after changing
`slkField_t`, rebuild the executable as well as `libsheet`: the schema array resides
in the executable and its stride must agree with the library. During this investigation,
the initial stale binary crashed there; `make openwarcraft3` rebuilt it and both menu
captures then ran successfully.

See also [UI flow](architecture/ui-flow.md), [MDX events](mdx-event-objects.md), and
[environmental fog](environmental-fog.md).
