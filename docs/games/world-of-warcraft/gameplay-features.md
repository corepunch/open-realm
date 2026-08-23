# WoW Gameplay Features

Reference for implemented and missing gameplay features, based on comparison with WoWee (data/WoWee) as the feature checklist.

WoWee is a C++ client that connects to AzerothCore/TrinityCore servers over the real WoW protocol.  
Our engine is a standalone Q2-style server+client — all game logic lives in the server module.  
Features here use the Q2 svc_layout / server-authored UI pattern, not WoWee's ImGui approach.

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
| HUD: health/mana bars, targeting frame | `g_ui.c` | Classic 1.12 layout; 1024×768 canvas |
| HUD: cast bar | `g_ui.c` | Server-side progress; counts down from cast_max |
| HUD: copper display | `g_ui.c` | Persisted on `wowEntityLocal_t.copper` |
| Minimap | `g_ui.c` | FT_MINIMAP viewport |
| Player portrait (2D per-race/sex) | `g_ui.c` | TemporaryPortrait-{sex}-{race}.blp |
| Character creation / selection | `games/world-of-warcraft/ui/` | Race, sex, class, appearance |
| First-login cinematics | `g_wow.c`, WoW UI XML | M2 camera playback via DBC chain |
| DBC loading (spells, items, areas) | `common/stb_dbc.h` | Shared schema-table decoder |

## World Labels

`renderer/r_ents.c` follows WoWee's `renderNameplates()` distance policy: ordinary NPC labels cull at 20 world units,
player labels at 40, and the selected target at 60. Each tier fades over its final 5 units. Distance is measured from the
interpolated third-person camera eye, not merely from clip-space visibility; projecting every server-authored name produced
readable labels for NPCs 50–1,650 units away and severe overlap.

The reusable camera-angle math lives in `games/world-of-warcraft/common/wow_view.h`; keep client camera construction and
renderer label culling on that same pitch/yaw/roll convention.

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
