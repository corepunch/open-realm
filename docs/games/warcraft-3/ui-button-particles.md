# UI button particles: retail data and demo DLL evidence

The autocast command-button sparkles and quest-button notification sparkles are authored MDX particle effects.
Retail code chooses when to show them and attaches sprite frames to buttons; the models contain the emitter
parameters, colors, fading, and animated positions. They do not require a separately invented perimeter-particle effect.

## Authoritative assets

Both local archives, `data/warcraft3demo/war3.mpq` and `data/Warcraft III/War3.mpq`, contain these
`UI/war3skins.txt` entries (lines 367, 369, and 430 in the inspected files):

| Skin key | Value |
|---|---|
| `CommandButtonAutocast` | `UI\Feedback\Autocast\UI-ModalButtonOn.mdl` |
| `CommandButtonActiveHighlight` | `UI\Widgets\Console\Human\CommandButton\human-activebutton.blp` |
| `QuestChangedParticles` | `UI\Feedback\QuestButton\UI-QuestButtonOn.mdl` |

The model files in the archives have `.mdx` extensions. The active-command highlight is a separate texture,
not the autocast particle effect. Both particle models reference `Textures\HeroLevel-Particle.blp`.

The extracted demo and retail models are byte-identical:

| Model | Bytes | SHA-256 |
|---|---:|---|
| `UI-ModalButtonOn.mdx` | 2216 | `9c2888e2be2edf90f5e1b2453b268305ff219ac61267523520306f1cef5ee0d4` |
| `UI-QuestButtonOn.mdx` | 1586 | `7775dd44bdde18eb5039abf5fcdb8b2187f0ae1b5439c04c1de78fd38ffbca3a` |

## Model definitions

Both models contain one looping `Stand` sequence, interval 833–2500 ms, no geosets, and `PRE2` emitters.
Autocast has **four** emitters (`BlizParticle01`, `BlizParticle03`, `BlizParticle02`, `BlizParticle04`);
quest has **two** (`BlizParticle02`, `BlizParticle04`).

Shared authored parameters: speed 0.02, variation/latitude/gravity 0, lifespan 1 second, emission rate 50 per
second per emitter, width/length 0, additive filter 1, texture atlas 1×1, head-or-tail selector 2 (both head and tail),
tail length 0.3, middle segment time 0.5, node flags `0x29000`. These are raw file values before renderer transforms.
Autocast colors are yellow/gold, with alpha segments 255/255/0 and decreasing particle scale.
Quest has a grey/white emitter (alpha 255/255/0) and a gold emitter (255/128/0).

Each emitter has its own pivot and a linear `KGTR` translation track, without a global sequence.
Autocast tracks have keys at 833, 1667, and 2500 ms. For example, `BlizParticle01` translates
`(0,0,0) -> (0,-0.035280,0) -> (-0.035277,-0.035280,0)`; its pivot is approximately
`(0.037109,0.037141,0.018743)`. Other emitters trace complementary sides of the button.
Quest tracks have five keys at 833, 2167, 2233, 2433, and 2500 ms, tracing long edges and rounded corners.
For example, its first emitter translates from zero to `(0.074839,0,0)`, then
`(0.077264,-0.002216,0)`, `(0.077264,-0.013491,0)`, and `(0.075034,-0.014958,0)`.
Thus motion paths and tail settings are present in the assets. Exact screen-space direction also depends on
sprite transforms and the generic particle renderer. See the implementation verification below.

## Demo DLL call sites

Inspected `data/warcraft3demo/Game.dll`, SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`.
Addresses below are virtual addresses for image base `0x6f000000`; these code/string file offsets equal VA minus base.
Names for unnamed functions are inferred from call flow and embedded source paths, not exported symbols.

| Consumer | Key string VA | Instruction loading key | Lookup call |
|---|---|---|---|
| Autocast | `0x6f569edc` | `0x6f1bb4ba` | `0x6f1bb4bf` |
| Quest changed | `0x6f5690f4` | `0x6f19a568` | `0x6f19a56d` |
| Active highlight | `0x6f569e74` | `0x6f1bb21b` | `0x6f1bb226` |

All three call the same keyed lookup at `0x6f0cbf70` (hash lookup followed by string comparison).
The autocast setup starts at `0x6f1bb470`; quest sprite creation at `0x6f19a510` is guarded by a null
check of the existing sprite. Embedded allocation source paths identify the callers as `CCommandButton.cpp`
and `CUpperButtonBar.cpp` respectively.

Both particle paths allocate a 0x170-byte sprite and call constructor `0x6f019170`, whose embedded source path
is `engine\Source\Frame\CSpriteFrame.cpp`. It installs vtable `0x6f4edd00`.
Both pass the lookup result to vtable slot `+0xec`, resolving to `0x6f0199a0`, the shared sprite model-loading
path. Autocast stores its sprite at owner offset `+0x184`; quest uses `+0x11c` and attaches it to the button
at `+0x118`. This is direct code-reference evidence connecting the skin definitions to UI sprite consumers.
The complete activation/deactivation lifecycle and generic retail particle simulation were not decompiled.

## Warsmash checkout

`data/WarsmashModEngine/core/src/com/etheller/warsmash/viewer5/handlers/w3x/ui/MeleeUI.java`, around line 1216,
calls `setSpriteFrameModel(autocastFrame, getSkinField("CommandButtonAutocast"))` and centers/sizes the sprite
on the command button. `CommandCardIcon.java`, around line 99, selects `STAND` when autocast is enabled
and sequence `-1` when disabled. It uses the authored model, not a bespoke hardcoded sparkle path.
No reference to `QuestChangedParticles` or `UI-QuestButtonOn` was found in this checkout; this observation does
not establish behavior in other Warsmash revisions/forks.

## Reproduction and tool limitations

```sh
build/bin/mpqtool -mpq data/warcraft3demo/war3.mpq cat UI/war3skins.txt
build/bin/mpqtool -mpq data/warcraft3demo/war3.mpq cat UI/Feedback/Autocast/UI-ModalButtonOn.mdx > /tmp/wc3-autocast.mdx
build/bin/mpqtool -mpq data/warcraft3demo/war3.mpq cat UI/Feedback/QuestButton/UI-QuestButtonOn.mdx > /tmp/wc3-quest.mdx
build/bin/mdxtool -mpq data/warcraft3demo/war3.mpq -model UI/Feedback/Autocast/UI-ModalButtonOn.mdx --info
```

Repeat extraction using `-mpq 'data/Warcraft III/War3.mpq'` and compare hashes.
The demo archive's listfile cannot currently be enumerated: `mpqtool` reports unsupported compression mask
`0x08`. Named extraction of these assets nevertheless succeeds, even though opening the archive prints that warning.
Use the retail archive's `ls UI/Feedback` for discovery; do not interpret the listfile failure as absent assets.

`mdxtool --info` currently reports only two autocast emitters. Its `PRE2` branch uses
`CountVariableSizeEntries`, which advances by size plus four, but these entries use inclusive sizes.
Directly walking the 1340-byte autocast `PRE2` payload yields four 335-byte records;
the 734-byte quest payload yields two 367-byte records. Each record begins with its inclusive size,
then its node's inclusive size. The emitter scalar block starts at record offset `4 + node_size`.
The diagnostic count bug remains separate from the renderer changes described below.

See also [diagnostic tools](../../diagnostic-tools.md) and [UI authoring](../../ui-authoring.md).

## Reviewed open-realm quest-button branch

Inspected the GitHub comparison `corepunch/open-realm:main...sookyboo:open-realm:quest_button` on
2026-09-14, head `d2c9337c560eb0959f79e473c4f240d216d0a90f`, base
`c063178ae6807a080f8cc2ab98487ecb0b78a059`, through the GitHub compare API and head file contents.
[Comparison](https://github.com/corepunch/open-realm/compare/main...sookyboo:open-realm:quest_button).

The new quest sparkle path does **not** load the authored model:
`SCR_LayoutDrawFrame` checks `onclick == "quests"`, then invokes the new renderer export
`DrawUIAttentionParticles(screen)` unconditionally for that button. `renderer/r_particles.c` implements
`R_UIAttentionPosition`, `R_UIAttentionUpdate`, and `R_DrawUIAttentionParticles` with a dedicated global
simulation, two opposite-phase emitters, manually defined rectangular perimeter motion, sizes, fading,
and head/tail lifetimes. Motion uses `0.9 * 0.18` cycles/sec (~6.17 seconds per full perimeter).
The tail draws white-texture UI quads; heads use the renderer's generated default dot texture. This path
reads neither `QuestChangedParticles` nor the model's `KGTR`, `PRE2`, or particle texture.
There is no quest-change activation/acknowledgement gate at the added draw call.

The same diff separately changes generic MDX tail origins to include the node pivot and adds a synthetic
transform test. That is distinct from its new procedural UI effect; touching the MDX renderer does not
make `R_DrawUIAttentionParticles` model-driven. The pivot change was inspected statically, not validated
against a rendered retail scene. No claims about authorship or AI use can be inferred from this diff.


## OpenRealm implementation

`FlashQuestDialogButton` resolves `[QuestIndicatorTimeout].QuestIndicatorTimeout` from `UI\MiscData.txt`
(10 seconds in both inspected stock data and the test fixture). The demo's UI routine reads that key at
VA `0x6f19a623` and arms a timer after enabling the sprite controller. `Blizzard.j::QuestMessageBJ` explicitly
calls the native for discovered, updated, completed, and failed quest messages. Quest setters alone do not request it.

The native stores a per-player simulation-clock deadline (`client_s.quest_until`), honoring `currentplayer`
when JASS runs in a local-player context. Repeated requests renew it. `UI_ShowQuests` acknowledges the request;
the regular resource-bar refresh expires it. The deadline is runtime presentation state and is cleared on save restoration.
`G_UpdateClientResourceBars` iterates connected player slots: reserved player edicts deliberately have `inuse == false`.
The previous world-entity filter (introduced in `d294dd4c9`) skipped these clients, so a flash request changed state
without publishing a new console layout. The regression includes this exact reserved-edict condition.

`UI_WriteQuestIndicator` resolves the recipient's `QuestChangedParticles` skin entry and appends an `FT_SPRITE`
proxy to the console layout. Its origin is anchored to the real FDF Quests button's bottom-left; model-space pivots
and animation own the particle paths. It has no click handler and uses `UIFLAG_SPRITE_OVERLAY` to render after button
artwork and highlights. Other sprites retain the existing background ordering. The UI flags wire field now uses
`NFT_LONG`; protocol version 6 prevents mixing the previous 16-bit flag payload with the new one.

`UI_WriteCommandButtonFrame` appends the same kind of passive foreground sprite for `alternate_active`, resolving
`CommandButtonAutocast` through the recipient's skin. The sprite references the command frame's assigned number and
anchors its origin to that button's bottom-left. Each enabled button gets its own sprite. The existing ability-owned
autocast state feeds `G_BuildCommandButton`; a subsequent layout omits the sprite when that state is off. Disabled
commands retain the indicator while autocast remains enabled. The existing command-button shader glow still accompanies
the particles; adding the sprite does not replace the active-command highlight behavior.

`drawSprite_t` carries a stable UI owner and scope identity. MDX sprite instances retain independent emitter accumulators
and particle lists, sharing authored tracks/textures. `particleScene_t` borrows the shared particle pool while isolating
active lists: UI draws cannot advance or redraw world particles. Generation checks make pool resets invalidate retained
pointers safely; model release frees its sprite instances. Stale instances are reclaimed on subsequent use of the model.
Sprites retain the parent view's time and delta, and their particle pass uses an unmasked fog texture.

The shared MDX emitter implementation now lives in `r_mdx_particles.c`. PRE2 `FrameFlags` is the enum
**0 = head, 1 = tail, 2 = both**, confirmed by the local Warsmash MDLX parser and emitter submission code.
The former `FrameFlags == 2 -> MODEL_EMITTER_TAIL` conversion was incorrect. Tail particles are emitted from
pivot-relative positions and extend along particle velocity by `TailLength`; they are not ribbon history samples
following the emitter. Both primitives use the authored lifespan and color/size curves.
`R_EncodeParticleSize` preserves fractional model-space sizes in the shared byte curve with a value multiplier;
previous direct assignment made the UI models' sizes zero. M2 reuses the same conversion.

This change does not claim complete PRE2 format coverage (for example,
all model-space/XY-quad policies and authored atlas interval variants). The quest and autocast effects use the existing
supported tracks plus the corrected head/tail and size contracts. No procedural quest perimeter or quest-specific renderer
export is added, and no commit from the reviewed branch was cherry-picked.

### Verification

Regression coverage:

- `wc3_api.quest_flash_is_player_local_and_acknowledged`: explicit native dispatch, recipient isolation, renewal,
  acknowledgement, timeout, and connected-but-not-inuse player slots.
- `wc3_game.hud_quest_indicator_uses_skin_anchor_and_timeout`: skin lookup, model namespace, passive parent-relative
  overlay payload, and expiration.
- `wc3_game.hud_autocast_indicator_uses_skin_model_and_overlay`: autocast skin lookup, model namespace, overlay flag,
  exact parent/bottom-left anchors, separate owners for multiple buttons, disabled-but-enabled autocast, and on/off/on
  layout refreshes. Frame numbering is explicitly initialized so this test also runs independently.
- `wc3_game.hud_autocast_indicator_suppressed_when_off`: no sprite emitted when `alternate_active` is off.
- `net.sprite_overlay_survives_layout_delta` and `client_layout.sprite_overlay_draws_after_button_artwork`:
  actual packet codec and background/artwork/foreground draw ordering.
- `renderer_model.mdx_ui_particles_preserve_pivot_sizes_and_both_quads`: stock-shaped PRE2 data through the production
  emitter entry point, including two primitives, pivots, fractional size, and velocity-based tail.
- `mdx_ui.sprite_clock_and_particle_scenes_are_isolated`: sprite timing, independent instances, world/UI particle
  separation, and safe pool resets/release without a GPU.

`make test` passes with the autocast extension, including ROC and TFT engine runs
(25,461 assertions in 1,192 tests per edition). The focused `wc3_game.hud_autocast*` run passes 22 assertions
in two tests per edition. The full suite requires local UDP socket access for its network tests.
The autocast extension has headless payload coverage; its rendered placement has not yet been captured.
A hidden-window capture on `(2)OgreMound.w3m` confirms the authored white/gold sparkle over the Quests button border;
local capture: `screenshots/shot0176.jpg`. This checks composition/placement, not a frame-for-frame comparison with retail.

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' +vid_hidden 1 +set sv_cheats 1 +map 'Maps\(2)OgreMound.w3m' +jass FlashQuestDialogButtonBJ +screenshot 40 +com_frame_limit 500
```

The campaign opening can hide the HUD even while a flash request is active. Use a normal gameplay map for a bounded
placement capture. `com_frame_limit` counts main-loop iterations, while `screenshot N` counts rendered frames; leave
sufficient iterations for the scheduled capture. macOS visual checks require access to a display/OpenGL context.
