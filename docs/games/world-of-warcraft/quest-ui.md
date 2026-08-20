# Quest UI & Server Data

WoW game mode keeps in-game UI server-authored (Quake 2 `svc_layout` pattern).
The client never runs quest Lua scripts while `game_mode` is active.

## Architecture

```
AzerothCore SQL dumps                extract_quest_data.py
 ├─ quest_template.sql          ──►  games/world-of-warcraft/serverdata/
 ├─ quest_template_addon.sql         ├─ g_wow_local.h   (structs + API)
 ├─ quest_offer_reward.sql           ├─ build/generated/g_quests.c   (static tables)
 ├─ creature_queststarter.sql        └─ quest_spawns.csv   (reference)
 ├─ creature_template_model.sql
 ├─ creature.sql (positions)
 └─ quest_poi_points.sql

games/world-of-warcraft/serverdata/ ──►  game/g_wow.c (quest logic)
                                         game/g_ui.c  (quest dialog rendering)
                                         game/m_creature.c (quest giver spawning)
```

## Data Structures

Defined in `games/world-of-warcraft/game/g_wow_local.h`:

| Struct | Purpose |
|--------|---------|
| `WOWQUESTGIVER` | quest_id → creature entry, display_id, world position |
| `WOWQUESTOBJECTIVE` | quest_id → 2D objective position (server-side anchor) |
| `WOWQUESTKILLOBJECTIVE` | creature display_id + required kill count |
| `WOWQUESTDETAIL` | Full quest: title, description, objectives, reward text, XP, gold, prerequisites, kill objectives |
| `wowQuestState_t` | Per-player: quest_id, status enum, kill_progress[4] |

Quest status enum: `NONE → ACCEPTED → COMPLETE → REWARDED`

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
- 384×512 panel at canvas position (24, 104) on a 1024×768 reference grid.

Buttons depend on quest state:
- **New quest** (not in log): "Accept" button → `quest_accept <id>`
- **In progress** (ACCEPTED, objectives incomplete): no action buttons
- **Complete** (all objectives done): "Complete Quest" → `quest_complete <id>`
- **Always**: "Close" button → `quest_close`

Kill progress is shown as `"Creature: 5/10"` text when the quest has
kill objectives and the player's progress is tracked.

## Quest Logic (g_wow.c)

- `Wow_AddQuest(client, id)` — validates detail exists, log not full, not
  already accepted, prerequisite chain met (`prev_quest` must be in log).
- `Wow_CompleteQuest(client, id)` — only awards if `status == ACCEPTED`;
  adds `reward_xp` to `WOW_STAT_XP`, `reward_gold` to `WOW_STAT_COPPER`.
- `Wow_QuestAwardKillCredit(attacker, display_id)` — called from AI death
  handler; iterates all ACCEPTED quests, matches display_id against kill
  objectives, increments progress, auto-transitions to COMPLETE when all
  objectives are met.
- `Wow_FindQuestState(client, id)` — linear search of the quest log.

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

Quest givers retain the available-quest image index (`overhead_sprite`) on the
server entity at spawn via `gi.ImageIndex("Interface\\GossipFrame\\AvailableQuestIcon.blp")`.
When building a snapshot, the server copies the entity state and then calls
`Wow_CustomizeEntity` (the `CustomizeEntity` game callback invoked from
`SV_BuildClientFrame`) to set the per-client value:

| Condition | `overhead_sprite` sent |
|-----------|----------------------|
| Quest already in player's log (any status) | 0 (hidden) |
| Quest has `prev_quest` and that quest is NOT in log | 0 (chain locked) |
| Quest has `prev_quest` and that quest IS in log | sprite index (show "!") |
| Quest has no `prev_quest` (starter quest) | sprite index (show "!") |

`Wow_AddQuest` uses the same "prev_quest must be in log" rule, so the marker
and the accept gate are always consistent.

The client pipeline:
- **Network**: `NFT_SHORT` image index in `entityState_t.overhead_sprite`.
- **Client resolve**: `cl_view.c` — `re.overhead_sprite = cl.pics[ent->current.overhead_sprite]`. Image configstrings are loaded eagerly in `CL_PrepRefresh`; late arrivals are caught in `cl_parse.c` and reloaded in-place.
- **Render**: `R_GameRenderModel` (`games/world-of-warcraft/renderer/r_game.c`) draws it after the model, floating `(M2_GroundOffset(model) + M2_HeadHeight(model)) * scale + 0.25` above the entity origin. `M2_HeadHeight` (`renderer/m2/r_m2.c`) returns `model->bounds.max.z` (the M2 file bounding-box top). `R_DrawBillboardSprite` (`renderer/r_particles.c`) draws the camera-facing quad via the particle billboard pipeline.

Turn-in "?" markers are not yet implemented.

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
