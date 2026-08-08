# World of Warcraft server data

`weapons.csv` contains the classic level-1 weapon rows used as the initial
weapon-data set by the WoW game module. Values are copied from the following
AzerothCore `item_template` columns:

```text
entry, name, subclass, displayid, inventory_type, item_level,
required_level, dmg_min1, dmg_max1, dmg_type1, delay
```

Source: `data/azerothcore-wotlk/data/sql/base/db_world/item_template.sql`,
upstream [AzerothCore item_template.sql](https://github.com/azerothcore/azerothcore-wotlk/blob/master/data/sql/base/db_world/item_template.sql).
The source repository's GPL license and authorship notice are preserved in
`data/azerothcore-wotlk/LICENSE` and `data/azerothcore-wotlk/AUTHORS`.

The current runtime uses entry 37 (`Worn Axe`) for the player's starting
weapon. The other rows are retained so class-specific starter equipment can be
wired without replacing the data format.

`quest_spawns.csv` contains the first-map-0 placement for 17 quest givers and
26 quest objective POIs from the starter quest chain. Giver rows preserve the
AC creature entry, model display ID, full position, and orientation. Objective
POIs provide X/Y only, so the runtime resolves their Z from terrain. Nearby
givers are spawned as non-hostile NPCs; nearby objective POIs are spawned as
server-side objective anchors identified by quest ID.

The rows were extracted from `creature.sql`, `creature_template_model.sql`,
`creature_queststarter.sql`, `quest_poi.sql`, and `quest_poi_points.sql` in the
same AzerothCore source tree.
