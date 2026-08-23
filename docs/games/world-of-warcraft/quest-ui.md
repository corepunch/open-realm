# Quest UI & Server Data

WoW game mode keeps in-game UI server-authored (Quake 2 `svc_layout` pattern).
The client never runs quest Lua scripts while `game_mode` is active.

## Architecture

```
common/shared.h                 ─── svQuestEntry_t, playerState_s.quest_log  (engine)
server/sv_quest.h               ─── SV_QuestFind, SV_QuestAdd                (engine)

AzerothCore SQL dumps                extract_quest_data.py
 ├─ quest_template.sql          ──►  games/world-of-warcraft/serverdata/
 ├─ quest_template_addon.sql         ├─ g_wow_local.h   (WoW structs + API)
 ├─ quest_offer_reward.sql           ├─ build/generated/g_quests.c   (static tables)
 ├─ creature_queststarter.sql        └─ quest_spawns.csv   (reference)
 ├─ creature_template_model.sql
 ├─ creature.sql (positions)
 └─ quest_poi_points.sql

games/world-of-warcraft/serverdata/ ──►  game/g_wow.c (quest logic, uses sv_quest API)
                                         game/g_ui.c  (quest dialog rendering)
                                         game/m_creature.c (quest giver spawning)
```

## Data Structures

### Engine-level (common/shared.h + server/sv_quest.h)

The quest log is part of `playerState_s` — generic, game-agnostic per-player state:

| Type | Location | Purpose |
|------|----------|---------|
| `svQuestStatus_t` | `common/shared.h` | `NONE → ACTIVE → COMPLETE → REWARDED` |
| `svQuestEntry_t` | `common/shared.h` | `quest_id` + `status` (one slot in the log) |
| `ps.quest_log[SV_MAX_QUEST_LOG]` | `playerState_s` | Fixed-capacity per-player quest log |
| `ps.quest_count` | `playerState_s` | Number of active log entries |
| `SV_QuestFind(log, count, id)` | `server/sv_quest.h` | Linear search by quest_id |
| `SV_QuestAdd(log, &count, max, id)` | `server/sv_quest.h` | Add with duplicate guard |

### WoW-specific (games/world-of-warcraft/game/g_wow_local.h)

| Struct | Purpose |
|--------|---------|
| `WOWQUESTGIVER` | quest_id → creature entry, display_id, world position |
| `WOWQUESTOBJECTIVE` | quest_id → 2D objective position (server-side anchor) |
| `WOWQUESTKILLOBJECTIVE` | creature display_id + required kill count |
| `WOWQUESTDETAIL` | Full quest: title, description, objectives, reward text, XP, gold, prerequisites, kill objectives |
| `wowClient_t.kill_progress[SV_MAX_QUEST_LOG][4]` | Per-slot kill progress counters (parallel to `ps.quest_log`) |

## Server Commands

| Command | Effect |
|---------|--------|
| `quest [id]` | Opens quest dialog. Without ID, uses selected NPC's quest_id. |
| `quest_accept <id>` | Adds quest to log (checks prerequisites, max 16 slots). Closes dialog. |
| `quest_complete <id>` | Awards XP + gold if quest status is ACCEPTED. Closes dialog. |
| `quest_close` | Hides dialog and quest log. |
| `questlog` | Toggles quest log panel. |

## Quest Dialog UI

The dialog is rendered on `LAYER_QUESTDIALOG` using classic QuestFrame textures:
- `Interface\QuestFrame\UI-QuestGreeting-{TopLeft,TopRight,BotLeft,BotRight}.blp`
- 384×512 panel at canvas position (0, 104) on a 1024×768 reference grid.

`Interface\FrameXML\QuestFrame.xml` remains the authoritative geometry even
though game mode does not execute it:

| Element | Classic metric |
|---------|----------------|
| NPC portrait | `(7, 6)`, 60×60, selected giver model via `FT_PORTRAIT` |
| NPC name | 300×14, centered at `TOP + (0, -23)` in the metal title bar |
| Close button | `(326, 14)`, 32×32, `UI-Panel-MinimizeButton-Up` |
| Quest title | `(28, 91)`, 18px `MORPHEUS.ttf`, black |
| Description | `(28, 116)`, 13px `FRIZQT__.TTF`, black, 270px wide |
| Scrollbar | `(329, 81)`, 16×334; 16px arrows and thumb, cropped to the texture center |
| Accept | `(23, 418)`, 77×22 |
| Decline | `(267, 418)`, 78×22 |

`UIPanelButtonTemplate` does not use the complete 128×32 button texture. Its
authoritative UV rectangle is `(0, 0)..(0.625, 0.6875)`, an 80×22 opaque region
which must be cropped and stretched across the button frame. Full UVs leave the
remaining transparent atlas area inside the frame and make the backdrop appear
to cover only part of the button.

The server-authored layout reuses `FT_SCROLLBAR` rather than adding a WoW-only
frame type. WoW sends the 10-byte `uiScrollBarImage_t`: three texture resources
and one shared UV crop from `UIPanelScrollFrameTemplate`. Its deliberate limits
are one visual state, no track backdrop, a shared UV rectangle, and square
arrow/thumb parts inferred from the frame width plus `UI_PIXEL_ASPECT`. Legacy
FDF scrollbars retain the full `uiScrollBar_t` backdrop payload and use the same
client drawer.

`FT_TEXTAREA` is the scroll viewport even before wheel/button interaction is
implemented. The client draws it with both `DRAW_WORD_WRAP` and `DRAW_CLIP`,
using the inset content rectangle as the clip rectangle. Wrapping without
clipping allowed long quest descriptions to paint through the footer buttons;
the corrected font height made that latent error immediately visible.

WoW's UI scene is normalized 1x1 while its authoritative FrameXML grid is
1024x768. Consequently one equal normalized X/Y span is not physically square:
glyph Y metrics and inferred square control heights use `UI_PIXEL_ASPECT = 4/3`.
Without it, both fonts and scrollbar parts render 25% too short vertically; the
title then also appears shifted upward because its glyph baseline offset is
compressed. Explicit `PW`/`PH` frame dimensions are already axis-correct and do
not receive this factor again.

`UIPanelButtonTemplate` uses 12px `GameFontNormal`: RGB `(1.0, 0.82, 0)`, or
byte color `(255, 209, 0)`. `GameFontHighlight` is white and `GameFontDisable`
is 50% grey; the current server-authored quest buttons serialize the normal
gold state.

Layout frame payload lengths are unsigned wire bytes. Cast `MSG_ReadByte` to
`BYTE` when decoding `uiFrame_t.buffer.size`; its return type preserves signed
behavior for legacy callers, and sign-extension otherwise rejects payloads from
128 through 255 bytes. The original 192-byte textured scrollbar payload exposed
this boundary before it was reduced to the compact 10-byte representation.
See [Server-Authored UI Payloads](../../architecture/ui-payloads.md) for the
full postmortem and rules for new payload schemas.

Inspect the installed source directly with:

```sh
build/bin/mpqtool -data data/world-of-warcraft cat 'Interface/FrameXML/QuestFrame.xml'
build/bin/mpqtool -data data/world-of-warcraft cat 'Interface/FrameXML/Fonts.xml'
build/bin/mpqtool -data data/world-of-warcraft cat 'Interface/FrameXML/UIPanelTemplates.xml'
```

Buttons depend on quest state:
- **New quest** (not in log): "Accept" button → `quest_accept <id>`
- **In progress** (ACCEPTED, objectives incomplete): no action buttons
- **Complete** (all objectives done): "Complete Quest" → `quest_complete <id>`
- **Always**: top-right close icon and "Decline" button → `quest_close`

Kill progress is shown as `"Creature: 5/10"` text when the quest has
kill objectives and the player's progress is tracked.

## Quest Logic (g_wow.c)

- `Wow_AddQuest(client, id)` — validates detail exists, prerequisite chain met
  (`prev_quest` must be complete, not merely active); delegates to `SV_QuestAdd`
  for log management.
- `Wow_CompleteQuest(client, id)` — uses `SV_QuestFind`; only awards if
  `status == SV_QUEST_ACTIVE`; adds `reward_xp` to `WOW_STAT_XP`, `reward_gold`
  to `WOW_STAT_COPPER`.
- `Wow_QuestAwardKillCredit(attacker, display_id)` — called from AI death
  handler; iterates `ps.quest_log`, matches display_id against kill objectives,
  increments `wc->kill_progress[slot][j]`, auto-transitions to `SV_QUEST_COMPLETE`
  when all objectives are met.

Quest log access: `ent->client->ps.quest_log` / `ent->client->ps.quest_count`
(via the engine's `playerState_s` — available to any server game module).

## Quest Spawning (m_creature.c)

`Wow_SpawnQuestLocations(origin)` spawns:
1. **Quest givers** — non-hostile NPCs with display model from DBC, positioned
   from the `wow_quest_givers[]` table. Their `quest_id` field is set so the
   `quest` command can read it from the selected entity.
2. **Objective anchors** — invisible server-side entities at POI positions,
   identified by `go_entry = quest_id`. Only entities within 6500 units of
   the player spawn are created.

Quest givers are creatures in AzerothCore and carry world positions via
`creature.sql`; the questgiver role is the join of `creature_queststarter`
(creature_entry → quest_id) with `creature` (creature_entry → spawn position).

`creature_queststarter` contains one row per quest, so Deputy Willem appears
five times at the same coordinates. `Wow_SpawnQuestLocations` collapses equal
`(creature_entry, position)` rows into one edict. At interaction/snapshot time,
`Wow_QuestForGiver` scans every matching row and chooses the first quest not in
the log whose prerequisite is complete. Without grouping, five overlapping
Willem models spawn and selection can open quest 18 (`Brotherhood of Thieves`)
instead of fresh-Human quest 783 (`A Threat Within`).

### Spawn budget and distance ordering

`WOW_QUEST_LOCATION_BUDGET` (default 32) caps how many quest giver entities are
spawned per `Wow_SpawnQuestLocations` call. The generated `wow_quest_givers[]`
table is ordered by quest_id; some starting quests have high IDs (e.g. quest 783
"A Threat Within" for humans) and would never be reached if the table were
walked sequentially — at the human spawn point ~566 givers fall within the 6500-
unit radius, meaning the budget runs out after the first 32 in table order.

**Fix:** `Wow_SpawnQuestLocations` does a pre-pass that collects all within-radius
candidates into a `wowGiverSort_t` array, sorts them by distance² (ascending),
then walks the sorted list applying the budget. Quest 783 is the 3rd closest
entry to the human spawn (2 units away, same tile as Deputy Willem's other quests)
and is therefore always spawned regardless of its position in the table.

## Overhead Quest Marker

Classic distinguishes the world marker models under `Interface\\Buttons` from
the small dialog-list icons under `Interface\\GossipFrame`. The yellow world
"!" is `TalkToMe.m2`; `AvailableQuestIcon.blp` is not its texture and must not
be drawn as a billboard. Quest givers register `TalkToMe.m2` once at spawn.
When building a snapshot, the server copies the entity state and then calls
`Wow_CustomizeEntity` (the `CustomizeEntity` game callback invoked from
`SV_BuildClientFrame`) to set the per-client marker:

| Condition | Marker sent |
|-----------|----------------------|
| Available | `model2=TalkToMe.m2`, `RF_ATTACH_OVERHEAD` (yellow "!") |
| Accepted, incomplete | `overhead_sprite=ActiveQuestIcon.blp` (temporary grey "?") |
| Complete | active sprite plus bit 15 gold tint (temporary yellow "?") |
| None | both marker channels cleared |

`Wow_AddQuest` uses the same "prev_quest must be complete" rule, so the marker
and the accept gate are always consistent.

The client pipeline:
- **Available marker network path**: existing `entityState_t.model2` (`NFT_SHORT`)
  plus `RF_ATTACH_OVERHEAD`; the client resolves it as `overhead_model` on the
  parent render entity. The WoW renderer draws it at
  `(M2_GroundOffset + M2_HeadHeight) * scale + 0.25`, using visual M2 bounds
  rather than collision radius. This is the same secondary-model state channel
  used by Quake-style entity effects and does not widen the snapshot struct.
- **Question-mark sprite path**: `entityState_t.overhead_sprite` remains the
  separate billboard channel. Image configstrings load eagerly in
  `CL_PrepRefresh`; bit 15 is stripped before the `cl.pics[]` lookup and selects
  the gold tint.

### Marker Animation Ownership

`TalkToMe.m2` contains two 1533 ms sequences. Sequence 0 (`Stand`) animates its
root Z translation from `0` to `-0.089` and back; sequence 1 (animation ID 190)
animates from `+0.517` to `+0.427` and back. The overhead renderer currently
copies the parent creature's complete `renderEntity_t`, including `frame` and
`oldframe`, before replacing only its model. Creature frames use concatenated
model-local intervals, so `M2_FrameToPoseTime` interprets the creature's frame
against the marker's unrelated two-sequence table. Frames `0..1532` select the
low sequence and `1533..3065` select the sequence translated upward by about
half a world unit, producing an abrupt periodic vertical jump even though the
NPC origin and marker anchor remain constant. Confirm with:

```sh
build/bin/m2tool -mpq data/world-of-warcraft/interface.MPQ \
  -model 'Interface\Buttons\TalkToMe.m2' --info --anim 0
build/bin/m2tool -mpq data/world-of-warcraft/interface.MPQ \
  -model 'Interface\Buttons\TalkToMe.m2' --info --anim 1
```

The marker must own its animation selection and clock; do not pass the parent
creature's model-local frame through to `TalkToMe.m2`.

### Original 2004 Placement and Reveal Oracle

The original 2004 Deputy Willem interaction view places the stack tightly: the
marker dot sits above the name, and the name sits immediately above the helmet
plume. M2 attachment ID 18 is `AT_AboveChar_PlayerName`; reverse-engineered
client documentation identifies it as the point consumed by
`CGUnit_C::GetNamePosition`. Resolve the animated attachment with the entity's
model matrix after rebuilding its current bone pose. Runtime tracing on Deputy
Willem showed a stable `2.213`-unit origin-to-attachment delta across animation
frames and frame-clock wraps.

The label occupies attachment 18: its rectangle ends at the projected point.
When no name is known, the quest marker also starts directly at attachment 18.
When both are visible, only the marker moves upward by `TalkToMe.m2`'s
model-authored positive bottom clearance, leaving the attachment slot for the
label without introducing another tuned world constant. Do not use character
animation bounds here: `HumanMale.m2` spans Z `-0.855..2.986` in those bounds
but its visible rest vertices span only `-0.001..2.034`, which previously put
both elements far above the head. Confirm the source data with:

```sh
build/bin/m2tool -mpq data/world-of-warcraft/model.MPQ \
  -model 'Character\Human\Male\HumanMale.m2' --info
build/bin/m2tool -mpq data/world-of-warcraft/interface.MPQ \
  -model 'Interface\Buttons\TalkToMe.m2' --info
```

Reference: WhiteoutLib's M2 attachment table documents attachment 18 and its
original-client consumer:
`https://github.com/sc2-arcade-watcher/WhiteoutLib/blob/libmodkit/docs/M2_FILE_FORMAT_SPECIFICATION.md`.

WoWee and WowUnreal are not authoritative placement implementations. WoWee
uses custom constants (visual bounding radius times two plus `1.1` for the
marker, and `+2.3` for names); WowUnreal hardcodes a 200 cm nameplate offset and
does not implement the quest marker. Use them only as comparison engines, not
as the 2004 behavior source.

The target reveal lifecycle is per player: show only `!` before the first
interaction, show `!` plus the name once the NPC is selected/known, then retain
the known name and remove `!` after accepting the quest. The current state does
not implement this contract: `m_creature.c` authors the name configstring for
every viewer immediately, and `QUEST_MARKER_ACTIVE` replaces `!` with the grey
active-question sprite instead of clearing the marker.

## NPC World Names

NPC names do not exist in the client creature DBCs. `CreatureDisplayInfo.dbc`
and `CreatureModelData.dbc` resolve appearance/model data only; authoritative
names come from the generated AzerothCore `WOWCREATURE` table. At quest-giver
spawn, the server interns `WOWCREATURE.name` into `CS_GENERAL` and sends the
absolute configstring index through the otherwise-unused WoW
`entityState_t.image` field. `V_AddClientEntity` resolves that index to
`renderEntity_t.name`; the renderer projects the entity top through the active
view-projection and draws the green `Fonts\\FRIZQT__.TTF` label. The secondary
marker render entity clears its inherited name so each NPC receives one label.

## Extraction Tools

Two-stage pipeline: SQL → CSV → C.

### Stage 1: SQL → CSV (`extract_server_data.py`)

Extracts all level≤20 content from AzerothCore SQL into CSV files:

```sh
python3 data/WoWee/tools/extract_server_data.py              # default (level ≤ 20)
python3 data/WoWee/tools/extract_server_data.py --max-level 40  # more content
```

Produces: `weapons.csv` (1289), `quests.csv` (2737), `quest_spawns.csv` (1787+2558),
`creatures.csv` (29947 templates / 40213 model rows), `creature_spawns.csv` (13729).

Quest extraction preserves the complete SQL prose and `$B` paragraph breaks.
It must not truncate text or eagerly replace `$N`, `$c`, and `$r`: `g_ui.c`
expands those tokens from the active player's name, class, and race when it
authors the frame.

### Stage 2: CSV → C (`serverdata/gen_serverdata_c.py`)

Converts CSV into static C arrays compiled into the binary:

```sh
python3 games/world-of-warcraft/serverdata/gen_serverdata_c.py --output-dir build/generated
```

The generated C tables are included by `game/g_wow.c` from `build/generated/`.
When adding new content, update the extracted CSV and rebuild.

**Caveat:** AzerothCore's `RequiredNpcOrGo` field contains creature *entries*,
mapped to display IDs via `creature_template_model`. Item-collection quests
(`RequiredItemId`) need manual mapping to kill objectives.

## Client-Side References (WoWee)

The installed client FrameXML provides reference layout/dimensions:
- `Interface\FrameXML\QuestFrame.xml` — panel dimensions, anchors, child positions
- `Interface\FrameXML\QuestFrame.lua` — greeting/detail/progress/reward states
- `Interface\QuestFrame\UI-QuestGreeting-*` — 256/128-pixel panel art textures

These are NOT executed at runtime. They serve as reference for the server-authored
layout pixel positions and asset paths.

## Test Coverage

`tests/test_wow_game.c` covers:
- Server data integrity (giver count, positions, quest details)
- Quest HUD presence on correct layer
- Accept/reject/prerequisite flow
- Kill progress tracking and auto-complete
- Wrong creature / overflow / double-reward protection
- Quest log open/close toggle
- Complete button visibility based on status
- NPC interaction → dialog opening
- Quest chain unlock (12→13→14)
- Progress text in dialog textarea
- `quest_givers_receive_creature_frame_for_idle_animation` — Deputy Willem (display 2072) spawns with `overhead_sprite != 0` and `Stand` animation
- `quest_marker_is_hidden_after_quest_acceptance` — `Wow_CustomizeEntity` shows "!" for a fresh player on quest 783, hides it after `quest_accept 783`
