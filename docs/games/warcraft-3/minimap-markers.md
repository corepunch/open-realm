# Warcraft III minimap markers

OpenRealm keeps four minimap concepts separate: the authored gameplay minimap,
automatic live entity contacts, transient pings/alerts, and preview-only
`war3map.mmp` icons. This document covers the automatic live contact path.

See also [alerts and minimap pings](alerts-and-minimap-pings.md), [team
colors](team-colors.md), [fog and cinematics](fog-and-cinematics.md), and
[loading and assets](loading-and-assets.md), and the generic
[server-selected presentation contract](../../architecture/server-selected-effects.md).

## Ownership and engine boundary

WC3 gameplay owns contact classification and recipient visibility. Shared engine
code only transports opaque presentation values:

- `entityState_t.effect_flags` bits 13-15 are the generic
  `EFX_GAME_VARIANT_*` payload. WC3 interprets its `0..7` values as
  `wc3MinimapContact_t` in `games/warcraft-3/common/minimap.h`.
- `playerState_t.stats[UI_PLAYERSTAT_GAME_VARIANT]` is the generic local
  game-presentation value. WC3 uses it for the ally-colour filter and exposes
  the semantic alias `WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR` only under
  `games/warcraft-3/`.
- `renderEntity_t.owner`, `renderEntity_t.team`, `renderEntity_t.effect_flags`,
  `viewDef.player`, and `viewDef.game_variant` are generic renderer inputs.
  `client/cl_view.c` copies them without knowing WC3 marker classes or ally
  colour meanings.

This preserves the engine/game boundary in `ARCHITECTURE.md`: no WC3 marker
enum, skin key, or ally-filter meaning belongs in `common/`, `client/`,
`renderer/`, or `server/`.

The wire structs are not widened. The change assigns semantics to three
previously unused `effect_flags` bits and one existing `stats[]` slot; the
entity delta mask and `entityState_t`/`playerState_t` sizes are unchanged.
`tests/test_net.c` round-trips all eight values of the generic three-bit entity
variant.

`G_CustomizeEntity()` chooses exactly one automatic WC3 contact per recipient.
`game_export.IsSnapshotPriorityEntity` lets the game identify visible contacts
that should survive ordinary snapshot saturation; the server keeps those ahead
of non-contact entities while retaining the existing per-frame packet limit.
If prioritized contacts themselves exceed `MAX_PACKET_ENTITIES`, the nearest
prioritized contacts still win.

The contact values are:

- `WC3_MINIMAP_CONTACT_NONE`
- `WC3_MINIMAP_CONTACT_UNIT`
- `WC3_MINIMAP_CONTACT_BUILDING`
- `WC3_MINIMAP_CONTACT_HERO`
- `WC3_MINIMAP_CONTACT_GOLD_MINE`
- `WC3_MINIMAP_CONTACT_GOLD_ENTANGLED`
- `WC3_MINIMAP_CONTACT_GOLD_HAUNTED`
- `WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING`

The marker is presentation metadata, not a second simulation entity. Existing
snapshot/FOW/invisibility filtering remains authoritative. Dead units and units
that remain `RF_HIDDEN` after recipient-specific visibility handling publish no
automatic contact. The WC3 renderer only considers top-level render entities with
a non-zero snapshot entity number, so client-created secondary `model2` draw
entities cannot create duplicate contacts and generic client code need not mutate
the opaque variant.

## Unit-data classification

WC3 object data is evaluated on the game side before the renderer sees a
contact:

- `hideOnMinimap` (`uhom`) controls the ordinary unit/building representation.
- `hideHeroMinimap` (`uhhm`) independently controls the dedicated Hero marker.
  With only the Hero marker disabled, the entity falls through to its ordinary
  representation; when the generic minimap display is also hidden, no automatic
  marker is published. This matches the World Editor distinction: the Hero field
  hides the special Hero symbol, while the generic field controls the ordinary
  unit/building/Hero minimap display. Historical editor guidance likewise notes
  that both switches are required to remove a Hero from the minimap completely.
- `neutralBuildingMinimapIcon` (`unbm` / `nbmmIcon`) selects the special neutral
  building contact.

Gold-mine classification is data/ability driven. Natural resource mines use the
Gold Mine contact. Overlay mines carrying `Aegm` or `Abgm` select Entangled or
Haunted contacts respectively; a known overlay mine without either racial
ability uses the normal Gold Mine contact.

## Compatibility evidence

The two minimap-hide fields are independently documented World Editor additions,
not names invented by OpenRealm: Warcraft III patch 1.13 added `Stats - Hero -
Hide Hero Minimap Display`, while patch 1.15 later added the generic `Stats -
Hide Minimap Display` field. Preserved patch notes are available at
`https://warcraft.wiki.gg/wiki/Warcraft_III/Patch_1.13` and
`https://liquipedia.net/warcraft/Patch_1.15`. World Editor field documentation
describes the Hero field as hiding the special Hero symbol and the generic field
as controlling minimap visibility; historical editor guidance reports that both
are needed to completely hide a Hero. The resulting Hero-to-ordinary fallback is
covered by OpenRealm's contact-classification tests.

For colour presentation, Blizzard's classic recon documentation describes the
allied-colour minimap toggle making enemies red
(`https://classic.battle.net/war3/basics/recon.shtml`). Contemporary Frozen
Throne beta notes preserved at
`https://forum.dvdtalk.com/video-game-talk/271375-warcraft-iii-expansion-beta-test-printthread.html`
record the minimap relationship palette as self white, allies teal, enemies red,
and creeps black. The screenshot comparison that found
OpenRealm's own contacts should be white is captured as an automated policy test
rather than relying on future manual recollection.

## Marker artwork and asset scope

Live special marker artwork is not hard-coded to file paths. WC3's renderer
loads the Game Interface values after `R_SetMapAssetScope()` has activated the
current map, resolving `war3mapSkin.txt` `[CustomSkin]` before stock
`UI\\war3skins.txt` `[Default]`:

| Contact | Game Interface key |
|---|---|
| Gold Mine | `MinimapResourceTexture` |
| Entangled Gold Mine | `MinimapEntangledResourceTexture` |
| Haunted Gold Mine | `MinimapHauntedResourceTexture` |
| Neutral building | `MinimapNeutralTexture` |
| Hero | `MinimapHeroTexture` |

The renderer reads these text files through its import filesystem, so map
archive precedence remains active. A malformed optional `war3mapSkin.txt` is
diagnosed once; fields that cannot be read then use valid stock `[Default]`
values. A missing/empty field or a resolved path that fails to load is diagnosed
and uses the renderer's visible placeholder texture.
There is deliberately **no** demotion from a missing special icon to an
ordinary unit/building marker: that would hide an asset/data error and violate
the repository's no-silent-fallback rule.

Stock skin textures use the normal pinned texture lifetime. Map-skin overrides
use the renderer's streaming texture cache and are reclaimed on the next map
registration if no persistent renderer consumer has pinned them. INI cache
parses own their table/text allocations; `Stb_IniCacheFree()` releases them.
The fixture MPQ includes all five stock keys and a map-level Hero override.

Loading/game-setup previews remain a different path. Existing
`war3map.mmp` types still use their preview artwork and do not create live
contacts.

## Rendering and colours

`R_DrawMinimap()` draws live contacts after the fog texture and before the
camera outline/border. The existing world-to-minimap transform is reused, so
contacts share rectangular-map letterboxing with pings, camera projection, and
minimap input.

For ordinary unit/building contacts:

- the local player's contacts are white in every ally-filter state;
- state `0` leaves non-local contacts in their already-resolved presentation
  team colour (`renderEntity_t.team`);
- states `1` and `2` use relationship colours for non-local contacts: ally teal,
  hostile red, and neutral player slots/creeps black.

`G_SelectionRelation()` and `EF_NEUTRAL` retain their selection/hover meaning:
an allied player without shared control can still be selection-neutral. The
minimap policy does not treat that flag as a neutral creep. It recognizes
neutral player slots as black, recipient-relative `EF_HOSTILE` as red, and
other non-local owners as teal. `EF_NEUTRAL` is published only for hoverable
entities where its existing world/hover semantics require it. The WC3 renderer
does not reconstruct alliance policy from team numbers.
Hero artwork receives relationship tint when the minimap ally-colour filter is
active. Mine and special neutral-building artwork retains its authored icon
colour.

`SetAllyColorFilterState` is local presentation state stored in
`WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR` (the WC3 meaning of the generic
`UI_PLAYERSTAT_GAME_VARIANT`). `currentplayer` calls affect only that player's
value; calls outside a local-player context update all clients. Values are
clamped to the three WC3 states `0..2`. State `2` currently has the same minimap
result as state `1`; retail's separate world-model recolouring for state `2` is
not implemented by this marker work.

`Get/SetCreepCampFilterState` remains the pre-existing placeholder state. It is
not moved into the minimap presentation stat because creep-camp aggregate
markers are not implemented yet.

## Marker size calibration

OpenRealm's WC3 UI uses FDF canvas units, not raw pixels. A prior patch passed
Warsmash-reference pixel values directly to `R_DrawImage()`, which made markers
several UI canvases wide. A later comparison used the inner minimap rectangle in
a retail screenshot and an OpenRealm screenshot to normalize away the different
screenshot resolutions; that comparison showed the corrected first pass still
about twice retail size.

The current screenshot-calibrated FDF sizes are therefore:

| Marker | FDF size |
|---|---:|
| ordinary unit | 0.002 x 0.002 |
| ordinary building | 0.005 x 0.005 |
| Gold/Entangled/Haunted/neutral special | 0.0105 x 0.0105 |
| Hero | 0.014 x 0.014 |

`wc3_minimap_marker_size()` and `wc3_minimap_marker_rect()` own the size and
centered-rectangle path used by the renderer. `wc3_minimap.marker_sizes_match_retail_capture_calibration`
locks the values and rectangle geometry into an automated test. These values are capture-derived OpenRealm
calibration, not independently recovered retail engine constants; like-for-like
retail captures may refine them later without changing the snapshot contract.

`wc3_minimap.ordinary_color_policy_keeps_self_white` likewise records the second
visual regression found during comparison: self contacts must stay white instead
of using the local player's team colour.

## Layering with other minimap systems

The gameplay minimap order is:

1. authored `war3mapMap` image;
2. live fog overlay;
3. automatic entity contacts;
4. camera outline and minimap border;
5. client transient pings/alerts (`CL_DrawMinimapPings`).

Pings are one-shot attention events and do not use the automatic-contact
variant. Scripted `minimapicon` handles will need their own texture/tint,
attachment, lifetime, and fog-state contract rather than reusing these entity
bits.

## Known gaps

The automatic-contact patch intentionally leaves unrelated or insufficiently
verified systems separate:

- ally-colour state `2` does not yet recolour world models;
- Hero pulse timing/alpha is not retail-calibrated;
- the server still has a hard snapshot capacity; if visible minimap contacts
  alone exceed it, the nearest prioritized set is retained;
- creep-camp strategic markers and filter buttons are not rendered;
- `CreateMinimapIcon*`, `DestroyMinimapIcon`, visibility/orphan lifetime,
  `SetAltMinimapIcon`, and `UnitSetUsesAltIcon` are not implemented as live
  minimap objects;
- minimap waypoints are not implemented;
- exact per-marker dimensions and overlap precedence can still be refined by
  like-for-like retail captures;
- retail persistence of the user's minimap filter preference across map/session
  transitions is not implemented by this server-authored presentation state.

## Verification

Automated coverage is intentionally split by ownership:

- `tests/test_net.c` verifies the generic `EFX_GAME_VARIANT_*` wire packing,
  without WC3 semantic names.
- `games/warcraft-3/game/tests/t_api.c` verifies WC3 contact classification,
  Hero/generic hide behavior, mine variants, hidden/dead suppression,
  recipient relationship publication, ally-filter clamping, and
  `currentplayer` locality.
- `games/warcraft-3/game/tests/t_minimap.c` verifies the screenshot-derived
  marker-size and rectangle path, passive-ally customization through the
  minimap colour policy, fixture-MPQ stock/map-skin lookup, pinned versus
  map-scoped loader selection, placeholder selection, and stock-default
  resolution after an invalid optional map skin. The fixture test exercises
  the production asset resolver and loader selector without creating a GL
  context or calling the renderer's full `R_RegisterMap()` entry point.
- `tests/test_renderer_model.c` calls the production WC3 `R_RegisterMap()`
  against `tests.mpq` and its nested `MapOverlay.w3x` fixture. It verifies that
  the imported Hero texture is cached as streamed, then reclaimed when a second
  map has no override, while stock skin textures stay pinned. The renderer
  texture cache and map-registration function are production code; filesystem
  reads and GL texture loads are test imports, so this checks archive lookup
  and cache lifetime without a GL context or pixel upload.
- `games/warcraft-3/tests/test_server_net.c` verifies that a game-prioritized
  minimap contact survives ordinary entity snapshot saturation.

The focused WC3 checks can be run with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_minimap.*'
make test-wc3-engine WC3_PATTERN='wc3_api.customize_entity_*minimap*'
make test-wc3-engine WC3_PATTERN='wc3_api.ally_color_filter_*'
make test-server-net
```

The generic wire round trips remain part of `make test` (`net.game_presentation_variant_stat_roundtrips`
and `net.entity_delta_preserves_game_presentation_variant_bits`).

Visual verification is still required for framebuffer-only properties such as
final texture appearance, overlap, Hero animation/pulse, and future fine-scale
calibration. The automated tests are intended to prevent the two already-found
size and self-colour regressions from returning.
