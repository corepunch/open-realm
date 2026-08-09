# Server data

Game-server data imported from external server projects lives alongside the
game that uses it under `games/<game>/serverdata/`.

## WoW dataset

`games/world-of-warcraft/serverdata/` — classic starter data imported from
AzerothCore SQL dumps: weapons, quest givers, objective POIs, quest details,
creatures, and creature spawns.

## Audit queue

AzerothCore also exposes server-owned values still hard-coded or simplified
in the WoW module:

- `player_class_stats.sql` / `player_race_stats.sql`: level-1 health, mana,
  and base attributes; runtime currently uses fixed player health/mana.
- `creature_classlevelstats.sql`: creature level health, armor, attack power,
  and damage; creature setup currently uses simplified values.
- `spell_bonus_data.sql` and related spell tables: coefficient and scaling
  data; current prototype uses fixed projectile damage.
