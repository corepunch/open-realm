# Server data

Game-server data imported from external server projects belongs under this
directory rather than in engine or game logic files. Each dataset documents
its source, license, and import scope.

Current datasets:

- `world-of-warcraft/`: classic starter weapon damage rows imported from
  AzerothCore's `item_template` table, plus nearby quest giver and objective
  locations imported from the world and quest POI tables.

## Audit queue

The bundled AzerothCore data also exposes server-owned values that are still
hard-coded or simplified in the WoW module:

- `player_class_stats.sql` and `player_race_stats.sql`: level-1 health, mana,
  and base attributes; the runtime currently uses fixed player health/mana.
- `creature_classlevelstats.sql`: creature level health, armor, attack power,
  and damage; creature setup currently uses simplified values.
- `spell_bonus_data.sql` and related spell tables: coefficient and scaling
  data; the current prototype uses fixed projectile damage.

These are intentionally not imported in this change because each requires a
matching runtime state and combat-rule decision. They are the next serverdata
imports after weapon equipment is wired beyond the starter item.
