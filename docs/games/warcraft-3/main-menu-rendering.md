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

## Campaign backdrop fog (issue 639)

Campaign scenes use their own fog profile, independently of `MenuZFog`. The
`SinglePlayer_DrawCampaignBackdrop` path previously set `RDF_NOFOG` and never
populated the fog contract, leaving the prologue's trees and distant arches bright.

Later `CampaignStrings` files explicitly document and provide `BackgroundFogStyle`,
`BackgroundFogStart`, `BackgroundFogEnd`, and ARGB-byte `BackgroundFogColor` per
campaign. The campaign scalar descriptor table reads these values. Original RoC
1.00 `CampaignStrings` lacks them; its authored profiles live in `UI/MiscData.txt`
as `[TutorialZFog]`, `[HumanZFog]`, `[OrcZFog]`, etc. Those sections are consumed
only when the campaign has no explicit fog profile. No numeric defaults are invented.
The local RoC prologue specifies linear fog from 0 to 2000 with RGB (0,51,77).

The selected campaign supplies the renderer's existing linear start/end/RGB
contract. Style -1 disables it; other styles or non-finite/inverted distances do
not enable linear fog. Density and ARGB alpha are unused by that contract, as in
the main-menu path. Switching campaign or returning to the selector resolves the
new campaign's profile, so disabled fog cannot retain the previous scene's state.

`menu_fdf.campaign_backdrops_use_authored_fog_and_reset_between_scenes` drives
production screen loading, selection and drawing with archive fixtures. It fails
before the fix and covers non-stock RoC Tutorial/Human values, explicit TFT Human
values, disabled Orc fog, and switching back. `make test-menu`, `make test`, and
`make openwarcraft3` pass. A bounded local capture confirms blue prologue haze:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc -vid_hidden 1 +ui_skip_transitions 1 +menu_game +menu_single_player_campaign_tutorial +screenshot 30 +com_frame_limit 45
```

Animation phase, data version and aspect ratio vary; the capture is a composition
check rather than a pixel-identical retail golden.

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

See also [MDX material composition](mdx-materials.md), [UI flow](architecture/ui-flow.md), [MDX events](mdx-event-objects.md), and
[environmental fog](environmental-fog.md).

## Camera-facing MDX billboard geometry (issue 640)

The RoC Orc sun is model geometry, not a particle emitter or a replacement sky.
`OrcCampaign3D.mdx` geosets 21/22 use Yellow_Glow/Yellow_Star additive layers
on root billboard nodes 1–5. Their quads lie in the model YZ plane. The previous
`R_GetNodeGlobalMatrix` implementation used a fixed `(30,0,90)` camera rotation,
introduced in May 2026. Under the authored campaign camera it turned those quads
almost edge-on, rendering the sun as a narrow diagonal streak.

Billboard orientation now comes from the active view-projection matrix: normalize
its right/up rows, obtain the front normal from their cross product, then map
MDX +X/+Y/+Z to front/right/up. Remove entity scale before quaternion extraction.
The existing pivot-preserving parent rotation cancellation and authored local
animation remain in place. This works for world, authored model and rolled cameras
without adding fields to renderer or network contracts.

`renderer_model.mdx_billboard_faces_actual_camera_and_preserves_pivot` drives
`MDLX_BindBoneMatrices` for three camera directions, camera roll, rotated/scaled
entities and nonzero pivots. It failed with the fixed camera and also detected
the need to normalize scaled entity axes. The visual property is checked with:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc -vid_hidden 1 +ui_skip_transitions 1 +menu_game +menu_single_player_campaign_orc +screenshot 30 +com_frame_limit 45
```

The bounded local capture restores the circular sun. This same path also renders
parented Night Elf eye and pendant glows; those require child-hierarchy coverage
in addition to the root-bone sun case. Fog is independently owned by campaign
menu data; see the campaign fog section when comparing background contrast.

## Parented eye and pendant billboards (issue 641)

The RoC model `UI/Glues/SinglePlayer/NightElfCampaign3D/NightElfCampaign3D.mdx`
already authors both reported effects; no generated artwork or replacement material
is needed. Relevant zero-based records in the locally inspected `War3.mpq`:

| Material | Texture | Filter / alpha | Layer flags | Nodes |
|---|---|---|---|---|
| 15 | campaign `MoonCrescentGlow.blp` | Additive / 0.45 | `0x31` | non-billboard 261, 285 |
| 16 | `Textures/lensflare1Ax.blp` | Additive / 0.7 | `0x31` | billboard 262, parent necklace 261 |
| 17 | campaign `NightElfFemaleEyeGlow1.blp` | Additive / 0.3 | `0x21` | billboards 265, 266, 275, 276, parent Head 264 |

`0x21` means Unshaded + Unfogged; `0x31` also adds TwoSided. These flags do not
mean NoDepthTest (`0x40`). Eye nodes and pendant flare have no local animation
tracks; their animated parent transforms determine their positions. Billboard
orientation cancels inherited rotation while retaining the parent's transformed
pivot, then faces the active camera. The eye ancestry is Root 462 → SpineLow 136 →
SpineUp 137 → Neck 263 → Head 264. Those ancestors have rotation tracks and no
scale tracks. The pendant's ChestR 200 ancestor also has small uniform breathing
scale. Scale/shear cannot explain missing eyes in this asset.

The missing glows share issue 640's fixed `{30,0,90}` billboard camera root cause.
The camera-basis fix restores the eyes and pendant flare together; separate
material/alpha changes would mask that cause. Issue 641 additionally fixes parent
rotation extraction: normalize the inherited matrix axes before converting them
to a quaternion. Parent scale otherwise skews the cancellation rotation, including
the pendant's authored breathing scale. This preserves inherited size and the
animated pivot; only the rotation used to cancel inherited orientation changes.

`renderer_model.mdx_parented_eye_and_pendant_billboards_face_camera_without_losing_animated_pivots`
drives `MDLX_BindBoneMatrices` with a rotating parent, two offset billboard children,
nonzero entity rotation/translation, three animation poses, two camera rolls and
parent scales 1, 1.01 (stock-shaped) and 2.3 (non-stock). It checks all three world axes and transformed pivots, then clears a child's
billboard flag to verify normal parent-animation inheritance. On pre-fix main,
36 facing-axis assertions failed while pivot preservation passed. With the shared
camera-basis fix, the extended scaled-parent cases still failed 72 facing-axis
assertions. All 340 assertions pass after normalizing the parent rotation axes.
The fixture contains no retail assets.

```sh
make test-renderer-model test-renderer-shadows
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +vid_hidden 1 \
  +menu_game +menu_single_player_campaign_night_elf \
  +screenshot 100 +com_frame_limit 120
```

The bounded local RoC framebuffer capture made during the shared billboard fix
confirmed both glowing eyes and the pendant flare. These are animation-phase
comparisons, not pixel-identical retail goldens. Network and save formats are
unchanged.
