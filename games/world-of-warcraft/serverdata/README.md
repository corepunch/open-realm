# World of Warcraft server data

## Directory layout

```
games/world-of-warcraft/serverdata/
├── weapons.csv            ← 1289 weapons (level ≤ 20) from AzerothCore
├── quests.csv             ← 2737 quests (min_level ≤ 20) with full text/rewards
├── quest_spawns.csv       ← 741 quest giver positions + 2558 objective POIs
├── creatures.csv          ← all 29947 creature templates / 40213 model rows
├── creature_spawns.csv    ← 13729 creature world positions on map 0
└── README.md
```

**CSV files** are authoritative AzerothCore extractions. Weapons and quests are
level-scoped; creature templates and model variants are complete and unfiltered.

The C tables are generated into `build/generated/` from the CSVs by
`serverdata/gen_serverdata_c.py` during WoW builds. Do not edit them by hand.

## Build workflow

The CSV files and `gen_serverdata_c.py` are source files; the generated C files
are build artifacts. The Makefile declares each generated file as a prerequisite
of the WoW game library and WoW game tests:

1. `make game-wow` or a WoW test checks the CSV and generator timestamps.
2. Missing or stale tables are regenerated into `build/generated/`.
3. `game/g_wow.c` includes `g_creatures.c`, `g_quests.c`, and `g_weapons.c`
   from that directory, so the tables are compiled into the game module.
4. `make clean` removes the generated tables with the rest of `build/`.

To force a clean regeneration:

```sh
make clean
make game-wow
```

To regenerate manually without compiling:

```sh
python3 games/world-of-warcraft/serverdata/gen_serverdata_c.py \
    --output-dir build/generated
```

Do not add generated C files under `games/world-of-warcraft/game/` or edit files
under `build/generated/` by hand.

## CSV sources

All CSV data is extracted from `data/azerothcore-wotlk/data/sql/base/db_world/`
using `data/WoWee/tools/extract_server_data.py`:

| CSV | SQL tables |
|-----|-----------|
| `weapons.csv` | `item_template` (class=2, RequiredLevel≤20, non-test) |
| `quests.csv` | `quest_template`, `quest_template_addon`, `quest_offer_reward`, `creature_queststarter`, `creature_template_model`, `quest_poi_points` |
| `quest_spawns.csv` | `creature` (giver positions), `quest_poi_points` (objectives) |
| `creatures.csv` | `creature_template`, `creature_template_model` |
| `creature_spawns.csv` | `creature` (map=0 positions for all extracted creatures) |

## Extraction

```sh
# Full extraction from SQL → CSV (≈30 seconds):
python3 data/WoWee/tools/extract_server_data.py

# Custom weapon/quest level cap (creatures always remain complete):
python3 data/WoWee/tools/extract_server_data.py --max-level 40

# Refresh the complete creature pipeline only:
python3 data/WoWee/tools/extract_server_data.py --only creatures
python3 games/world-of-warcraft/serverdata/gen_serverdata_c.py --only creatures

# Generate C from CSV manually (the build does this automatically):
python3 games/world-of-warcraft/serverdata/gen_serverdata_c.py --output-dir build/generated
```

## CSV column reference

### weapons.csv
```
entry, name, subclass, displayid, inventory_type, item_level,
required_level, dmg_min, dmg_max, dmg_type, delay, quality
```

### quests.csv
```
quest_id, title, description, objectives_text, reward_text,
reward_xp, reward_gold, reward_item1, reward_item2, prev_quest, min_level,
kill_obj1_display, kill_obj1_count, kill_obj2_display, kill_obj2_count,
kill_obj3_display, kill_obj3_count, kill_obj4_display, kill_obj4_count,
creature_entry, poi_x, poi_y
```

### quest_spawns.csv
```
kind, quest_id, creature_entry, display_id, x, y, z, orientation
```

### creatures.csv
```
All 55 `creature_template` columns, followed by:

model_idx, display_id, display_scale, model_probability, model_verified_build
```

There is one joined CSV row per `creature_template_model` variant. Creatures
without a model retain one row with `\N` model fields. SQL `NULL` is preserved
as `\N`; empty strings remain empty strings.

### creature_spawns.csv
```
entry, map, zone, area, x, y, z, orientation, wander_distance
```

## Notes

- AzerothCore `RequiredNpcOrGo` uses creature *entries*; the CSV maps them to
  display IDs via `creature_template_model`. Item-collection quests
  (`RequiredItemId`) have 0 for kill objectives and need manual game-design
  mapping.
- The GPL license from AzerothCore is preserved in `data/azerothcore-wotlk/LICENSE`.
- Creature spawns include all map=0 positions; the runtime spawns only nearby
  entities within budget using `Wow_SpawnAmbientCreatures()`.
