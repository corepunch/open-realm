# Campaign loading and asset resolution

For measured menu/loading resource residency and reclamation priorities, see [WC3 memory](memory.md).

## Loading-screen ownership

`CL_BeginLoadingMap` publishes the resolved destination and freezes a non-interactive loading plaque.
The client remains `ca_connected` while registering assets; the first usable server frame activates it once
`CL_PrepRefresh` has completed. `SCR_DrawCursor` hides both the native and authored cursors during loading.

`SV_Map` calls the mandatory game export `PrepareMap` before `LoadMap`. WC3's `G_PrepareMap` reads only W3I/WTS
through `CM_ReadMapInfo`, binds native `UI/FrameDef/Glue/Loading.fdf`, and serializes `UI_WriteLoadingLayout`
against that temporary metadata. `G_MapString` is shared with gameplay's `G_LevelString` so early chapter text
uses the same WTS rules. A custom W3I loading model takes precedence; otherwise the campaign background number
selects a model and sequence from `UI/WorldEditData.txt`'s `LoadingScreens` section. Maps without either use the
decorated `LoadingMeleeBackground` skin entry.

The initial transport order is:

1. Loading-phase configstrings: destination, asset scope, models, images, and fonts.
2. `svc_loading_screen` chunks: receiving the complete compressed layout decodes the frame tree, registers
   the preceding media, and repaints immediately.
3. Full configstring pages, then baseline pages.
4. The final `svc_mirror "precache"` permits world/model/image/sound registration; completion queues `begin`,
   then the first usable frame activates gameplay.

`SV_BuildLoadingScreen` compresses the complete authored layout into map-owned `sv.loading`. The payload is the
existing layer byte, delta-encoded UI frames (including text and type-specific buffers), and frame terminator.
It accepts only `LAYER_LOADING`, not a nested server-message stream. Compression and decode are bounded by
`MAX_MSGLEN`; there is no configstring-slot budget, zero padding, or text shortening. Failure to fit or compress
fails map startup with a diagnostic. Referenced media remain in the normal model/image/font configstring pools.

Protocol version 4 replaces the binary loading configstrings with this explicit message:

| Field | Wire type | Meaning |
|---|---|---|
| Opcode | byte | `svc_loading_screen` |
| Total | 32-bit integer | Complete compressed byte count, positive and at most `MAX_MSGLEN` |
| Offset | 32-bit integer | Chunk offset in that stream; zero starts/restarts assembly |
| Size | 32-bit integer | Positive chunk byte count, bounded by the remaining stream and packet |
| Data | `Size` bytes | The corresponding compressed bytes |

`SV_SendLoadingScreen` accounts for the 13-byte header and preceding media when splitting at `SV_SignonLimit`.
UDP packets stay within `BZ_SIGNON_SIZE` (1,400 bytes); loopback uses the engine buffer limit. The client allocates
only the declared total, requires contiguous offsets and a consistent total, and releases the assembly buffer
on completion, error, or disconnect. Corrupt compressed data, truncated packets, and missing/out-of-order chunks
are rejected. This framing does not add retransmission or sequencing to the existing raw UDP transport.
Client and server must both use protocol version 4.

Three resource-pool endpoints are retained in `sv.loading_end` before gameplay adds resources. Media indices
remain stable as `LoadMap` extends the pools. `SV_New_f` replays the retained compressed layout and reconstructs
its media dependencies from the authoritative configstrings using those endpoints. The server releases the
compressed layout on map replacement, lobby entry, or shutdown.
The batch includes inherited Loading.fdf template media declarations, but excludes later world/gameplay media.
WC3's background/bar are models, so moving only images earlier is insufficient.

For a listen server, `SV_Map` establishes the connection and sends this loading batch before `ge->LoadMap`, then
calls `CL_LoadingFrame`. This limited loading pump invokes no command buffer, client tick, server tick, or gameplay
callback. It also calls `SDL_PumpEvents` so Linux/Wayland/X11 window-manager events continue to be serviced while
the synchronous map load owns the main thread, without consuming queued gameplay input. WC3 passes this same pump
through `game_import.LoadingFrame`; the common W3 map parser yields between major archive sections and `G_LoadMap`
yields between world/HUD/entity/script phases; large entity-spawn loops also yield periodically. Dedicated servers
remain no-ops. `cl.precache_ready` prevents the early layout/`CS_WORLD` from starting bulk registration until the full
configstring/baseline handshake reaches `precache`.

`CS_ASSET_SCOPE` and `re.SetAssetScope` establish map-import resolution before renderer world registration.
This matters for custom loading MDX models whose companion textures live inside the destination archive.
The normal registration pass retains those loaded handles; reconnect clears them at the existing client boundary.

Both background and progress bar retain the FDF's `FT_SPRITE` type. The background uses `#!sequence` for native
screen-space geometry. The zero-size bar uses `#0` plus `UI_STAT_LOADING_PROGRESS`, a client-local normalized
binding, so `SCR_LayoutDrawSprite` supplies `#0@ratio`. No player-state field or network layout size changes.
`FT_LOADING_BAR` is the image-bar contract used by WoW; an image and a model may have the same numeric index.

### Melee loading panels, minimap, and roster

`Loading.fdf` has distinct custom/campaign and melee panels. `LoadingTitleText` intentionally lives on the
right; `LoadingMeleeMapName` is anchored above `MinimapImage`. The old code always hid `LoadingMeleePanel`,
even for melee artwork. Choose the melee panel when W3I supplies neither a custom model nor a campaign background.
The border around the minimap is part of the background MDX, so an empty border does not prove a minimap frame drew.
No authored title/minimap geometry needs overriding.

`G_PrepareMap` applies the same `gi.ApplyLobbySettings` used by `G_LoadMap` to its temporary W3I metadata before
serializing loading. Native `LoadingPlayerSlot` clones show participating human/computer names and selected races;
empty/closed slots and neutral players are excluded. Names resolve WTS for direct launches and use lobby names
for configured games. Columns repeat the FDF container width and row height. Team headings, player-name colors,
and synchronized per-player ready highlights are still separate presentation work; no ready state is fabricated.

`FT_MINIMAP` plus `UIFLAG_MINIMAP_PREVIEW` uses frame text as the destination archive reference. The game renderer
loads `war3mapMap.blp` (or an authored TGA) and `war3map.mmp` directly, before terrain registration. This static path
never samples fog, draws the camera outline or live pings, or registers a clickable gameplay minimap. W3I's
`hide_minimap_in_preview_screens` flag is respected. MMP v0 consists of two 32-bit header words and 16-byte records
(type, X, Y, BGRA). Coordinates refer to the full 256-pixel thumbnail, including letterboxing. Types 0/1/2 use the
stock gold, neutral-building, and Circle of Power textures. Start-marker colors are the authored MMP colors,
not lobby-remapped colors or final randomized spawn positions. Missing/corrupt assets are logged.

The first implementation stored the preview-bit result in `BOOL`, an unsigned byte. A bounded Bandit Ridge trace
showed flags `0x802a` arriving intact but the narrowed preview value becoming zero, drawing the unloaded gameplay
minimap's white texture. Use C `bool` for this high-bit membership result. The network test covers delta decoding
through the actual minimap draw dispatch, as server-only frame inspection cannot catch this narrowing.

A twelve-player Emerald Gardens layout measured 3,610 bytes before compression and 789 bytes compressed, exceeding
the original 512-byte limit even with text shortening. Protocol version 3 expanded that allocation to eight binary
slots. Protocol version 4 removes the slots and their text budget entirely: `svc_loading_screen` carries complete
native rows, lobby names, and minimap archive references within the engine message limit. Chunking bounds each
network packet. The core, loading-FDF, and server signon tests cover these contracts.

The local `data/WarsmashModEngine` checkout's `MenuUI.java` binds `LoadingMeleePanel` but keeps it hidden;
`internalStartMap` populates the custom panel. It is not a complete retail melee-loading implementation.

For an early framebuffer capture, queue `screenshot 2` before `map` in an exec script, then run bounded:

```sh
printf 'screenshot 2\nmap "Maps/FrozenThrone/(2)BanditRidge.w3x"\n' > /tmp/loading-check.cfg
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +vid_hidden 1 +com_frame_limit 30 +exec /tmp/loading-check.cfg
```

Unlike startup `+map` processing, the script orders the screenshot request before synchronous map preparation.
Screenshots are written under `screenshots/`. Bandit Ridge, a configured Twisted Meadows lobby, and twelve-player Emerald Gardens were visually
checked with visible, unfogged terrain and authored markers.

### September 7 initial-layout regression

Commit `aa5f89f73` moved the loading screen from the menu to the initial server layout but lost campaign-row
resolution and changed native sprites into portraits. A bounded Human02 trace confirmed background number `3`
was registered as nonexistent `LoadingMeleeBackground.mdx`, while the bar's portrait viewport was zero-sized.
Once images registered, the same model index (`1`) selected an unrelated image. Restoring native sprite types,
the ROC/TFT row parser, and an explicit progress binding fixes all three without changing FDF geometry.

### September 9 direct-launch timing investigation

Before the early batch, a bounded macOS Human02 trace showed the initial loading draw had no layout. The layout
arrived at 4.82 s, but background/bar model handles were still NULL at progress 0.10 and 0.40. World registration
preceded the entire model pass, so the first artwork was presented at 7.81 s and gameplay began at 8.63 s: only
about 0.8 s of loading artwork. Menu FDF initialization took about 61 ms. Earlier swaps were real but lacked content.
History identifies `aa5f89f73` (`wc3: move loading UI into initial layout`) as the late-publication boundary.

After the fix, a local Human02 run presented loading art at 4.61 s, immediately before entering bulk server loading
at 4.61 s; that work finished at 6.05 s. Its initial batch was 4405 bytes. WoW Azeroth follows the same early
batch path; its separate artwork defects and visual verification are covered in [WoW data loading](../world-of-warcraft/data-loading.md). These are individual hidden-window observations, not timing
guarantees: renderer/game initialization and loading-presentation metadata/assets still take startup time.

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' +screenshot 2 +map 'Maps/Campaign/Human02.w3m' +vid_hidden 1 +com_frame_limit 100
build/bin/openwow -data data/world-of-warcraft +screenshot 2 +map 0 +vid_hidden 1 +com_frame_limit 100
```

`run-map` does not forward `ARGS`, so use the binary for bounded diagnostics. To measure, temporarily probe
`SV_BuildLoadingScreen`, `CL_PrepLoading` after its swap, `ge->LoadMap` entry/return, and `SCR_EndLoadingPlaque`
with `SDL_GetTicks()` / `fprintf(stderr, ...)`; remove the probes afterward. Hidden-window rendering still needs
the Mac display server; a sandbox without displays fails before map loading and gives no useful timing.

Quake III's analogous principle is client registration of presentation before expensive loading:
[`CG_DrawInformation`](https://github.com/id-Software/Quake-III-Arena/blob/master/code/cgame/cg_info.c) obtains the
map name from server info and registers `levelshots/<map>.tga`, while `CG_LoadingString` repaints through
`trap_UpdateScreen`. Its [download path](https://github.com/id-Software/Quake-III-Arena/blob/master/code/client/cl_main.c)
handles referenced PK3 downloads before cgame initialization; it does not stream a rendered loading screen.
Our server-authored layout uses the existing media configstrings instead of Quake III's levelshot naming convention.

### June 28 regression

Commit `b6349c392` removed `cl.loading_map`/`GetLoadingMap` but left `CL_BeginLoadingMap` ignoring its
`mapName` argument. UI lookup could only see a startup `+map` cvar, explaining why direct map launches
worked while campaign buttons produced a black background plus LOADING. Runtime diagnostics for Human01
showed a valid requested destination and an empty UI map cvar. In addition, the UI looked up its own
cached path, preventing invalidation on subsequent maps. Both paths now use the published destination.
TFT has an additional independent schema difference: ROC `LoadingScreens` rows are
`label,sequence,model`, while TFT rows are `expansion-category,label,sequence,model`, including ROC campaigns
under TFT archives. Fixed ROC indices interpreted HumanX01's sequence `6` as a filename. `UI_ParseLoadingRow`
consumes the optional numeric category and validates the sequence/model fields; malformed rows log their key.

In that menu-owned implementation, the loading-state draw check had to precede standalone-screen dispatch because
`menu_ingame` is queued asynchronously. The initial-layout path now dispatches loading from `SCR_DrawScreenField`.

### Historical menu FDF registry isolation

`libmenu` and `libgame` intentionally have separate `stb_fdf` frame registries.  The parser implementation functions are hidden per shared library, and the `frames[]` backing store must be hidden as well.  On ELF/Linux, exporting `frames` allows normal symbol interposition to alias the two registries even though each library defines its own copy.  `G_LoadMap -> UI_ResetHud -> UI_ClearTemplates` then clears/reuses the menu's live `Loading`/`LoadingBar` slots during `SV_Map`; cached non-NULL loading-frame pointers survive but no longer describe the loading sprites, so later progress updates reach the old `M_DrawLoadingScreen` without reaching the MDX renderer.

A characteristic failure is that the initial loading sprite renders before `SV_Map`, while later progress values change without reaching the MDX sprite renderer. Verify registry isolation before changing progress math or swap/present code.

## Level-transition asset ownership

A campaign `ChangeLevel` is a **map replacement**, not a mutation of the current world. The client clears its
map-owned renderer handles before the next registration sequence, while `G_LoadMap` clears the server-side model
metadata cache because `CS_MODELS` indices restart for each `SV_Map`. The WC3 map loader releases the previous
map's W3I/WTS/object-modification allocations, placements, terrain arrays, and pathing storage before reading the
destination archive. These boundaries prevent a model index, trigger string, placement, or pathing allocation from
retaining the previous level by accident.

The WC3 renderer likewise replaces its map state at `_W3M_RegisterMap`: old terrain segment/layer VAOs, ground
layer lists, cliff model choices, fog render targets, minimap handle, terrain shadow, and source W3E arrays are
released before the destination world is built. Ground and cliff lookup caches are map-scoped and reset at the
same boundary. Do not append a new campaign map to the previous terrain lists.

### Map-imported model and texture overrides

The MPQ reader supports nested archive paths: a lookup such as
`Maps\Campaign\Human03.w3m\Textures\Foo.blp` opens `Human03.w3m` and then resolves the inner path. WC3 installs
the currently registered map as the renderer's generic map-asset scope, so ordinary model and texture references
resolve with this order:

1. `<current .w3m/.w3x>\<logical Warcraft asset path>`;
2. the ordinary base-data path.

The resolved scoped path is also the renderer cache key. This is required for consecutive maps that import
different bytes under the same logical name; a `Textures\Foo.blp` cached for Human02 must not satisfy Human03's
own `Textures\Foo.blp`. Missing scoped models/textures fall back to Blizzard data without turning the scoped miss
into a missing-resource placeholder. Texture fallbacks alias the scoped key to the resolved base texture so later
lookups do not repeat the nested-archive miss. Clearing client state clears the generic renderer asset scope so a
map import cannot continue overriding menu assets after disconnect.

Standalone renderer regression coverage exercises both a present scoped model/cache hit and a missing scoped
model that falls back to the base path, including scope clearing at a registration boundary.

This is intentionally narrower than a full Warsmash-style data-source stack. The current transition still does
**not** rebuild all WC3 SLK/TXT data per map, merge all `war3map.w3a/.w3t/.w3b/.w3d/.w3q` object modifications,
implement JASS `Preload`/`Preloader`, or expose staged byte/task loading progress. `war3map.w3u` now applies the
registered `UnitProfile`/`UnitUI` subset (including Required Animation Names and custom model paths), but the remaining
Balance/Data/Weapons/Abilities tables and true per-map `war3mapMisc.txt`/skin overlays remain separate data-layer work;
do not infer those capabilities from the renderer's map-import lookup.
## Loading progress contract

Loading progress is client-owned and intentionally coarse. `CL_BeginLoadingMap` resets `cl.loading_progress` to
zero. `CL_PrepRefresh` advances it monotonically at existing registration boundaries and
`SCR_UpdateLoadingPlaque` explicitly repaints the otherwise frozen Quake-style loading plaque after each advance.
The initial loading batch carries the `Loading.fdf` tree as `svc_layout`; its `FT_SPRITE` bar binds
`UI_STAT_LOADING_PROGRESS` to the `LoadingProgressBar` MDX sequence using `#0@ratio`. No player-state/network field is involved.

Current phase values are:

| Progress | Completed boundary |
| ---: | --- |
| 0.05 | local listen-server `SV_Map()` returned successfully (local games only) |
| 0.10 | `CS_WORLD` is available and client refresh preparation has started |
| 0.40 | collision/world map and renderer map registration are ready |
| 0.60 | model configstrings are registered |
| 0.75 | image configstrings are registered |
| 0.87 | sound configstrings are registered |
| 0.94 | font configstrings are registered |
| 0.98 | client `begin` has been queued |
| 1.00 | sound registration is closed and `cl.refresh_prepped` is true |

These are **phase milestones, not byte percentages or time estimates**. In particular, local `SV_Map()` /
`ge->LoadMap()` remains synchronous and publishes only its completed boundary, not progress from inside its
server/game loading work, so the already-visible bar remains at its initial position during a long map load. Do not manufacture sub-percentages for that work;
add progress only when an owned lifecycle can report a real boundary. The plaque stays visible at 100% until the
first usable server frame promotes the client to `ca_active` and `SCR_EndLoadingPlaque` runs.

## Asset names are data

| Source | Meaning / resolution |
| --- | --- |
| `Units/DestructableData.slk`, `texFile` | `_` (or empty/absent) means no replacement image. Preserve an authored extension; do not append `.blp` in the spawn code. Human01 `LT05` uses `ReplaceableTextures\Cliff\Cliff0.tga`; crates/gates use `_`. |
| Texture references ending in `.tga` | The exact file wins. If absent, `R_ReadTextureFile` tries the same stem with `.blp`, then normal renderer diagnostics/placeholders apply. This handles converted source names without overriding real custom TGA files. |
| `ReplaceableTextures\CameraMasks\White_mask.tga` | Human01's cinematic script requests this; the local ROC archive contains `White_mask.blp` (1422 bytes), not TGA. The cliff BLP is 90592 bytes. |
| Native `StandardTemplates.fdf` | Some unused templates reference art absent from ROC archives (`HeavyBorder*`, `LightBorder*`, `ButtonBackGround`, `ButtonCorners`, `GlueScreen-PlayerButton-BorderRight`, `GlueScreen-ROC-EditionButton-*`). Parsing records texture keys/indices; `UI_GetTexture` loads only on consumption. Do not replace missing art with invented paths or suppress renderer warnings for consumed resources. |

`UI_GetTexture` caches both real and placeholder renderer handles. Decorated names are re-evaluated on theme
changes; an unchanged theme does not reload. Parsing or inheriting a template alone must not fetch its art.

## Cliff-transition classification

Terrain vertices are ordered NE, NW, SE, SW by `GetTileVertices`; model configuration letters use
SW, NW, NE, SE. A transition requires exactly two **adjacent ramp corners one cliff level apart**.
Two same-height flagged corners belong to an adjoining ordinary cliff, not a sloped transition.

Human01 cell `(64,31)` yielded model-order levels `[1,0,1,1]` and ramp flags `[1,0,0,1]`.
The former `GetTileRamps(tile) > 1` test built nonexistent `CliffTransHABH0.mdx` and omitted its geometry.
Correct classification selects the ordinary `CliffsBABB0.mdx`. The native transition directory contains
L/H and H/X edge pairs, not an HH or LX edge. Do not turn file-not-found into a guessed terrain fallback.
The [Warsmash terrain loader](https://github.com/Retera/WarsmashModEngine/blob/main/core/src/com/etheller/warsmash/viewer5/handlers/w3x/environment/Terrain.java)
is a useful secondary reference for separating transition edges from regular cliff cells; local archive
contents and logged vertex metadata establish this case.

## Diagnostics and verification

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Units/DestructableData.slk
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' ls ReplaceableTextures/CameraMasks
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' ls Doodads/Terrain/CliffTrans
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat UI/FrameDef/Glue/StandardTemplates.fdf
build/bin/openwarcraft3 -data 'data/Warcraft III' +map 'Maps/Campaign/Human01.w3m' +com_frame_limit 100
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +map 'Maps/FrozenThrone/Campaign/HumanX01.w3x' +com_frame_limit 100
make test-renderer-model test-ui
make test
```

For the menu regression, launch without `+map`, then select Single Player → Campaign → Human → the desired mission. The console
registers `menu_single_player_campaign`, but `menu_single_player_campaign_human` is a UI-handler command,
not a console command. Campaign selection now enters `MissionSelectFrame`; the selected mission's
`menu_single_player_mission_select N` handler is what issues the map command. A diagnostic replay can temporarily
invoke those handlers after showing the campaign screen; remove the hook afterward. Queue `screenshot 2` before
the mission handler to capture the frozen loading plaque.
Use `+com_frame_limit 100` for bounded runs; engine screenshots appear under `screenshots/`.

Regression tests cover texture extension lookup and exact-file precedence, SLK replacement sentinels,
ROC/TFT loading-row schemas, rotated/equal-height/diagonal cliff edges, unused FDF art, lazy texture cache hits/misses and theme changes,
and native loading-sprite serialization, client progress binding, and separate model/image namespaces.
Campaign artwork needs ROC/TFT runtime verification against real archives. Use explicit `-roc` and `-tft`
when a saved `fs_expansion` setting might override the default.

See also [UI authoring](../../ui-authoring.md), [scene workflow](../../rendering-scene-workflow.md),
and [filesystem loading](../../fs-loading-architecture.md).

The `wc3_loading.initial_layout_resolves_campaign_custom_and_melee_art` test uses `tests.mpq`'s native
`Loading.fdf`, skin keys and `WorldEditData.txt` rows. It calls the production loader and writer, then inspects
emitted sprites, model configstrings, title/subtitle/body, sequence and progress binding. Cases cover ROC and
TFT row schemas, custom-model precedence, and default skin decoration. Restoring the pre-fix
`hud_loading.c` makes this test fail. The client drawing tests additionally check progress 0/0.5/1 and collisions
between model and image indices.

The shared `net.loading_batch_registers_media_before_full_precache` test verifies that a partial chunk cannot
publish the screen, initial model/image handles become available after complete assembly, later world media
remains deferred, and only the final `precache` reply opens the registration gate. Other client tests reject
corrupt/truncated/oversized payloads, missing chunks, and changed totals.
`server_net.loading_batch_precedes_world_and_retains_resource_indices` exercises a later connection against the
retained layout and resource endpoints. `make test-server-net` also covers complete text above the former 2 KiB
limit, binary preservation over real UDP within the signon budget, and maximum-size loopback chunking.
The WoW game test verifies that `PrepareMap` resolves and writes loading art before clearing/spawning the world.
The version-4 change passed the 156-test core suite, 32-test server network suite, and 59-test WoW game suite,
plus WC3/WoW/SC2 builds. A bounded Human02 launch with `-roc +vid_hidden 1 +com_frame_limit 100` reached
`CL_SendBegin` and `G_ClientBegin`; this confirms registration and gameplay entry, not visual asset completeness.

The September 9 configstring refactor was checked with `make test` (2,855 tests), builds of all three game
binaries, bounded Human01/Human02 runs, Human02 → Human02 → Human01 reloads, and WoW Azeroth map entry. The Human02
source layout measured 560 bytes (including its original opcode), and compressed to 303 bytes before removing that
opcode. Both chapters retained their complete text and native artwork in early engine screenshots. Neither needed
text shortening. Those measurements describe the historical binary-slot transport; the current transport uses
`svc_loading_screen` as specified above.

A bounded console-script run also exercised Human02 → Human02 → Human01, with 100 `wait` commands between map
commands. All three reached `G_ClientBegin`, and early screenshots showed the correct chapter/sequence at zero
progress, including the same-map reload. Use actual `map` commands in an `exec` config for this check: repeated
startup `+map` arguments are cvar assignments, so the last value wins instead of scheduling multiple transitions.

LAN loading uses bounded startup pages and an engine-owned client map reader; see [LAN lobby and startup](../../architecture/network.md#lan-lobby-and-startup) for the timeout, UDP-size, and remote-only import-crash fixes.
