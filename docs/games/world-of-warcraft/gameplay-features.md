# WoW Gameplay Features

Reference for implemented and missing gameplay features, based on comparison with WoWee (`data/WoWee`) as the feature checklist.

WoWee is a C++ client that connects to AzerothCore/TrinityCore servers over the real WoW protocol.
Our engine is a standalone server and client. Game logic stays in the game module.
Copy data contracts and rendering rules from WoWee. Do not copy opcodes, Warden, Vulkan, or their old ImGui panels.

## Upstream since the vendored snapshot

Checked 2026-10-09 against `https://github.com/Kelsidavis/WoWee`.

| | Commit | Date | Tag |
|--|--|--|--|
| Vendored `data/WoWee` | `3d52d5a48` | 2026-08-02 | v2.0.37-preview |
| Same tree on upstream (history was rewritten; other parent differs, tree matches) | `2f9cf5476` | 2026-08-02 | v2.0.37-preview |
| `origin/master` | `b8b6242d0` | 2026-10-03 | v3.1.42 |

3032 commits and about 1965 files (`+245k/−99k`) landed after that identical tree. August 2026 is the jump (v2.0.38 through v3.1.x). September is polish through v3.1.42. The worktree is clean but `master` has diverged from `origin/master` because of the rewrite, so a pull will not fast-forward. To read current code: `git -C data/WoWee fetch origin && git -C data/WoWee reset --hard origin/master`. The missing-feature table below was written against the August 2 tree.

v3.0.0 (2026-08-14) is the cut that matters. They stopped drawing their own interface and drive Blizzard FrameXML. The changelog groups roughly two thousand commits as API shape, events, and layout, then settings, world, picture, and sound. Use that changelog as a checklist. Do not merge the C++.

Drive our work from these, in this order:

1. **FrameXML answers.** Wrong arity raises; a missing function usually does not. Cases they hit: `GetTalentInfo` returns 10 values not 8, `AbandonQuest()` takes none, `PLAYER_LEVEL_UP` carries nine values, gossip is announced only after its text query, scroll frames ask for the wheel and never the mouse, a status bar's fill honors `SetDrawLayer`, and 83 sites anchor and measure in the same breath (the quest tracker collapses when the measure is zero). A stand-in that answers the wrong type is worse than nil. Layout is in a 768-tall virtual screen.
2. **One terrain surface.** Mesh, floor query, and clutter must sample the same height, including the center vertex of a chunk cell (v3.0.9). Ground-cover budget is per chunk, not a running total that spends the tile on the first rows (v3.0.1). Short foliage normalizes wind against its own height. Player passage bends clutter and then it springs back. Detail doodads keep their authored sequence. Shader wind does not also swing them.
3. **Collision and camera.** Stairs and hole-cut chunks stay under the heightfield. Tree trunks collide by height, not by a width test that drops conifers. Use authored collision instead of a name guess. The camera stops on WMO walls, floors, and ceilings and steps around a hit instead of only pulling in. Creature draw scale uses both scale fields. Doodad distance does not outrun the terrain under it. Tilted doodad rotation was tried four ways and reverted. Do not copy a guessed placement order.
4. **Character and items.** Helm attachment is slot 11, not 0. Circlets use `HelmetGeosetVisData` zeros so hair stays. `CharacterFacialHairStyles` variants are columns 6–8. Stat flyouts come from `gt*.dbc`. `CR_*` is one-based and the game tables are zero-based. Quest reward XP, honor, title, reputation, and spell are in the query plus `QuestXP.dbc` / `QuestFactionReward.dbc`, not hard zeros. The fail bit is beside the complete bit. A loot click addresses a stable slot id after earlier slots are removed.
5. **Leave on their side.** Opcode tables, update fields, Warden, SRP, battleground and guild-bank packet layouts, FSR, ray tracing, Android, and the asset extractor. Their ImGui panels are the interface they retired.

## Implemented

| Feature | Files | Notes |
|---------|-------|-------|
| Creature spawning | `m_creature.c` | Wolf/boar/kobold/murloc via DBC display lookup |
| Melee combat (auto-attack) | `g_ai.c`, `g_wow.c` | Swing animation → damage point timing |
| Ranged spells (Fireball, Frostbolt) | `g_wow.c` | Projectile entities + impact models |
| Healing Touch | `g_wow.c` | Self-heal via spell cast system |
| Pain / death animations | `g_ai.c` | M2-event-derived damage_point; Death/Dead sequences |
| Corpse entities | `g_gameobject.c` | 5-minute despawn timer |
| Quest system | `g_wow.c`, `g_ui.c` | Accept, progress tracking, completion, reward items via inbox |
| Quest kill credit | `g_wow.c` | `Wow_QuestAwardKillCredit` dispatched on death |
| Area triggers / dungeon loading | `g_wow.c` | `Wow_CheckAreaTriggers`; cross-map warp |
| Spawn-point selection (per race/class) | `g_playercreateinfo.c` | Generated from `playercreateinfo.csv` |
| Action bar (12 slots) | `g_ui.c` | Server-authored; spells mapped to slots |
| Backpack (16 slots) | `g_ui.c`, `g_wow.c` | Click backpack button to toggle; slots 0-5 also in HUD bar |
| **Loot system** | `g_ai.c`, `g_wow.c`, `g_ui.c` | Rolled on death, copper auto-taken; item click-to-take |
| **Damage flash overlay** | `g_ai.c`, `g_ui.c` | Yellow outgoing / red incoming; 1.5 s fade |
| HUD: health/mana bars, player frame | `g_ui.c` | `PlayerFrame.xml` geometry; 119px textured bars at x=87, values, portrait/name/level; transient rest icon awaits server state |
| HUD: cast bar | `g_ui.c` | Server-side progress; counts down from cast_max |
| HUD: copper display | `g_ui.c` | Persisted on `wowEntityLocal_t.copper` |
| Minimap | `g_ui.c` | FT_MINIMAP viewport |
| Player portrait (2D per-race/sex) | `g_ui.c` | TemporaryPortrait-{sex}-{race}.blp |
| Character creation / selection | `games/world-of-warcraft/menu/` | Race, sex, class, appearance |
| First-login cinematics | `g_wow.c`, WoW UI XML | M2 camera playback via DBC chain |
| DBC loading (spells, items, areas) | `common/stb_dbc.h` | Shared schema-table decoder |

## World Labels

`g_ui.c` sends a static `LAYER_WORLD_HOVER` name and health/mana tree during `ClientBegin`. `Wow_CustomizeEntity` grants
`EF_HOVER_HEALTH` and compressed vitals only to live selectable creatures. The generic client projects the hovered model's authored
overhead point and evaluates those bindings locally; mouse motion sends no network message. Vanilla-style recipient filtering keeps
the pooled name hidden until the creature is selected, so an unselected hover can show bars without disclosing its name.

## Loot System Details

**Architecture** (added 2026-08):
- `Wow_RollLoot(ent)` called from `Wow_AIDie`; populates `wowEntityLocal_t.loot_items[]` + `loot_copper`
- Hard-coded loot table in `g_wow.c` keyed by `display_id` (wolf/boar/kobold/murloc)
- On `interact` with a corpse → `Wow_OpenLootTarget`: snapshots items into `wowClient_t.loot_snap[]`, auto-takes copper, plays "Loot" animation (1.2 s)
- `loot` command finds nearest corpse within 10 units and calls `Wow_OpenLootTarget`; right-clicking a corpse entity via `select <n>` also triggers it
- `loot_take <slot>` command moves item to first free `inventory[]` slot; syncs removal back to corpse entity
- `loot_close` / auto-close when all items taken
- `UI_WriteLootWindow` in `g_ui.c` renders the panel from the client snapshot

**Loot table entries** (extend by adding rows to `wow_loot_table[]` in `g_wow.c`):

| display_id | Creature | Copper | Common drops |
|-----------|---------|--------|-------------|
| 161 | Wolf | 10-40 | Stringy Wolf Meat 80%, Wolf Pelt 40%, Light Leather 30% |
| 193 | Boar | 10-40 | Raw Boar Ribs 80%, Boar Tusk 35%, Light Leather 25% |
| 163 | Kobold | 5-25 | Linen Cloth 70%, Kobold Candle 25% |
| 188 | Murloc | 5-25 | Murloc Eye 55%, Linen Cloth 45% |

To add a new creature type: add a `wowLootEntry_t` row to `wow_loot_table[]` and ensure its display_id matches the `WOW_CREATURE_DISPLAY_*` constant in `g_wow_local.h`.

## Missing / Not Yet Implemented

Features WoWee has that we do not, ranked roughly by gameplay importance:

| Feature | WoWee files | Notes |
|---------|------------|-------|
| Item tooltips on hover | `ui/chat/item_tooltip_renderer.cpp` | Show item stats when cursor is on icon |
| Player stats window (Str/Agi/etc.) | Character panel | DBC `Chr_Races`, `Chr_Classes`, stat formulas |
| Spellbook window | `ui/spellbook_screen.cpp` | Show all known spells; clicking casts or drags to action bar |
| Talent window | `ui/talent_screen.cpp` | `TalentTab.dbc`, point allocation |
| Chat system | `ui/chat/` | Dozens of channel/command/tab sub-systems |
| Social panel (friends/ignore) | `ui/social_panel.cpp`, `game/social_handler.cpp` | |
| Minimap zones / POI | `rendering/world_map/layers/` | Zone text overlay, quest POI dots |
| World map window | `rendering/world_map/world_map_facade.cpp` | Full map + layer overlays |
| Vendor interaction | `pipeline/wowee_npc_services.cpp` | Buy/sell items |
| Trainer interaction | `pipeline/wowee_trainers.cpp` | Spend skill points |
| Mounts | `pipeline/wowee_mounts.cpp`, `rendering/animation/mount_fsm.cpp` | Mount/dismount animation FSM |
| Pets | `pipeline/wowee_pets.cpp` | Pet summon/feed/rename |
| Experience + leveling | Implicit in combat | Currently XP is static (120/400) |
| Player level scaling (health, damage) | `pipeline/wowee_stat_curves.cpp` | Level-based stat curves |
| Enemy level badges | Target frame | Level number + skull for ??-level |
| Target health bar | Target frame | Show selected enemy's HP |
| Buff/debuff display | `ui/combat_ui.cpp` | Aura icons above health bar |
| Cooldown tracking | `ui/combat_ui.cpp` | Overlay on action button icons |
| Floating damage numbers (3D) | `rendering/spell_visual_system.cpp` | Our current version is 2D overlay near frame |
| Emotes | `pipeline/wowee_emotes.cpp` | `/dance`, `/wave`, etc. |
| Swim / flight animations | `rendering/animation/locomotion_fsm.cpp` | |
| Dynamic creature spawns from DB | `pipeline/wowee_spawns.cpp` | We use AzerothCore CSV snapshot |
| Respawn after death | `g_wow.c` has `respawn` cmd | Needs death state, ghost form, graveyard TP |
| PvP flags | `pipeline/wowee_pvp.cpp` | |
| Group / party system | `game/social_handler.cpp` | |

## Adding a New Feature

1. Check WoWee's `src/` for the closest analog (game logic in `game/`, UI in `ui/`, data in `pipeline/`).
2. Our game logic lives in `games/world-of-warcraft/game/`; server-authored UI in `g_ui.c`.
3. New server data tables: add CSV to `serverdata/`, extend `gen_serverdata_c.py`, add to `WOW_GENERATED_SRCS` in `game.mk`.
4. Follow Q2 command dispatch pattern for player commands (see `Wow_ClientCommand` in `g_wow.c`).
5. Add tests in `tests/test_wow_game.c` for any new command or state transition.
