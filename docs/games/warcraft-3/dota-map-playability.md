# DotA Custom-Map Playability

OpenRealm cannot yet play Defense of the Ancients. A bounded dedicated run of
`Maps/DotA v6.83dAI PMV 1.42 EN.w3x` on commit `6f1057f1` dies during map load
with `SIGSEGV` in `jass_remove_comments(NULL)` before any simulated frame.

This document is the evidence and work order for that map. Reaching
`com_frame_limit` later will still not mean the game is playable.

## Contract

DotA 6.83d is a TFT custom scenario (W3I format 25, `gameDataSet` Custom). The
PMV 1.42 EN file is a protected Chinese AI modification of IceFrog 6.83d. The
script identifies itself as `6.83d.1.5`. Retail Warcraft III loads it because
Storm still hashes original member names; the `(listfile)` is fake binary and
must not be trusted.

Authoritative inner members (hash lookup, not the listfile):

| Member | Size | Role |
| --- | ---: | --- |
| `scripts\war3map.j` | 4,164,351 | Map script. There is no `war3map.j`. |
| `war3map.w3a` | 1,031,633 | 1,364 original-table ability modifications. Custom table is empty. |
| `Units\CampaignUnitFunc.txt` / `CampaignUnitStrings.txt` | 53,399 / 114,187 | 810+ map-local unit rows (heroes such as `Harf` Omniknight). |
| `Units\CampaignAbilityFunc.txt` / `CampaignAbilityStrings.txt` | 134,854 / 564,817 | ~490 map-local ability rows. |
| `Units\ItemFunc.txt` / `ItemStrings.txt` / `ItemAbilityFunc.txt` | 75,575 / 166,832 / 126,167 | 1,000+ item rows. |
| `war3mapMisc.txt` | 1,761 | DotA constants: `MaxHeroLevel=25`, custom `NeedHeroXP`, magic armor table, `MaxUnitSpeed=522`. |
| `war3map.w3e` / `war3map.doo` / `war3map.wpm` | terrain / doodads / pathing | Present. `war3mapUnits.doo`, `war3map.w3u`, and `war3map.w3t` are absent; units and items are script- and INI-authored. |

The archive is MPQ v1 at file offset 512 (`HM3W` user data). Header
`wSectorSizeShift` is **15** (16 MiB sectors). Storm's sector size is
`512 << wSectorSizeShift` (max shift 15). OpenRealm must honor that value;
clamping anything above 64 KiB down to 4096 makes `SectorTableLooksValid` reject
every multi-kilobyte compressed member. 1,299 files exist; 1,296 of them are
compressed+encrypted. One file uses PKWARE implode (`(listfile)`).

## Data Flow

```text
loose Maps/*.w3x
  -> CM_LoadMap opens the map as a nested MPQ (not added to the global FS stack)
  -> CM_ReadMapScript looks only for war3map.j
  -> G_SpawnEntities -> jass_dobuffer(level.mapinfo->mapscript)
  -> G_StartScripts calls main()
```

Gameplay sheets (`Units\CampaignUnitFunc.txt`, `war3mapMisc.txt`) go through
`G_ReadGameDataFile`: `Custom_V1\<path>` then the base MPQ path. They never see
members inside the current map archive. Renderer map-asset scope covers models
and textures only.

## Diagnostic Workflow

Bounded dedicated run (isolated home, unique UDP port):

```sh
XDG_DATA_HOME=/tmp/dota-home \
build/bin/openwarcraft3 \
  -data 'data/Warcraft III' -tft \
  +dedicated 1 +set game_port 28301 \
  +set com_fast_forward 1 +set vid_hidden 1 \
  +map 'Maps/DotA v6.83dAI PMV 1.42 EN.w3x' \
  +com_frame_limit 600
```

Observed on `6f1057f1`:

1. `CM_ReadDoodads: missing war3map.doo` and `CM_ReadUnitDoodads: missing war3mapUnits.doo`.
   `war3map.doo` is in the hash table; on builds that still clamp sector size
   `> 65536` down to 4096 (`common/mpq.c` before #435), open fails. Small
   members such as `war3map.w3i` still open (one sector under both sizes).
   After #435 the authored 16 MiB sector size is honored.
2. Overlay spam: `Custom_V1\Units\*.txt` and `Custom_V1\war3mapMisc.txt` miss and
   fall back to base TFT data. The map's own `Units\` and `war3mapMisc.txt` are
   never consulted.
3. `EXC_BAD_ACCESS` in `jass_remove_comments` at `jdo.c:1980` with `buf == NULL`,
   because `CM_ReadMapScript` stored a null `war3map.j`.

Hash-known names that `mpqtool cat` cannot open until sector size is honored
include `scripts\war3map.j`, `war3map.w3e`, `war3map.w3a`, and `war3map.doo`.

Do not use `make audit-wc3-maps` for this file. That enumerator only walks
retail campaign members inside `War3.mpq` / `War3x.mpq`.

## JASS Demand (from `scripts\war3map.j`)

The compiled script is 132,869 lines and 5,708 functions (Vexorian-style
obfuscated names, original native names preserved). `config` and `main` exist.

Against `games/warcraft-3/game/common.txt` (1,549 natives) and
`api_module.c` (965 registered callbacks):

| Bucket | Natives used by this map | Identifier refs |
| --- | ---: | ---: |
| Registered | 388 | majority of call sites |
| Unregistered | 137 | 12,465 |

Unregistered groups, by identifier count:

| Group | Natives | Refs | Why it matters |
| --- | ---: | ---: | --- |
| Hashtables (`InitHashtable`, `GetHandleId`, `Save*`/`Load*`/`HaveSaved*`/`FlushChild*`/`RemoveSaved*`) | 53 | 8,692 | DotA 6.83d's primary data model (YDWE / patch 1.24). Zero implementations in tree. |
| `GetObjectName` | 1 | 833 | Ability/item/unit tooltips and chat. |
| Multiboard | 17 | 1,022 | Scoreboard / KDA / player list. |
| `GetEventDamageSource` | 1 | 213 | Kill credit, lifesteal, on-hit scripts. |
| `SetUnitAbilityLevel` / `IncUnitAbilityLevel` / `UnitDamageTarget` / `GetUnitCurrentOrder` / `GetUnitLevel` / type flags | 10 | 510 | Skill ranks and spell scripts. |
| Shop (`GetSoldUnit`, `GetBuyingUnit`, `GetSellingUnit`, stock add/remove) | 6 | 192 | Side shop / secret shop / tavern. |
| TextTag | 10 | 225 | Gold/last-hit floating text. |
| `GetHeroStr` / `GetHeroAgi` / `GetHeroInt` | 3 | 123 | Attribute-scaling spells. |
| Item user-data / visibility / pawnable | 10 | 239 | Courier and stash. |
| Lightning / image / ubersplat | 13 | 90 | Skill presentation. |
| `StringHash` / `StringCase` / `StringLength` | 3 | 285 | Hashtable keys. |

A `gamecache G` global remains, and campaign game-cache natives are already
registered. This map's live state is hashtables, not game cache.

Registered-but-stub natives this script still calls include
`SetPlayerAbilityAvailable` (650), `SetUnitPathing`, `UnitApplyTimedLife`,
`PauseUnit`, `SetUnitInvulnerable`, and most sound-parameter setters. Treat
those as partial, not as coverage.

## How Far

Not close. The map does not finish loading. After the two load bugs below, the
script still cannot run: the first `InitHashtable` / `GetHandleId` is an
unimplemented native. After that, heroes, items, and abilities are map-imported
rows that the gameplay loader never sees, plus 1,364 `war3map.w3a` modifications
the object-data merge does not apply.

Suggested order (GitHub #431 and children):

1. Honor MPQ sector sizes above 64 KiB so protected maps open (#435).
2. Read `scripts\war3map.j` when `war3map.j` is absent; refuse null mapscripts
   instead of crashing (#433).
3. Implement the hashtable native family (#437).
4. Resolve map-archive `Units\*.txt`, `war3mapMisc.txt`, and `war3map.w3a`
   through the existing sheet/object-data path (mount the map as the highest
   FS archive, or read those members from the already-open map handle) (#432).
5. Shop, damage-source, hero-attribute, texttag, and multiboard natives
   (#434, #436).
6. Custom ability mechanics. Melee AbilityData work does not cover `A00Y`-style
   map abilities.

Parsing 4.1 MiB / 5,708 functions is unproven. Budget time for VM capacity
after the mapscript actually loads.

## Known Pitfalls

- The fake `(listfile)` is implode-compressed garbage. `mpqtool ls` prints it
  and looks like a corrupt archive. Hash lookup of known WC3 names still works.
- `CM_ReadHeightmap` is unused; terrain open failures are easy to miss because
  doodad errors print first.
- Overlay logs for missing `Custom_V1\...` are expected for this map and are
  not the same as "the map has no CampaignUnitFunc". The map has one; FS cannot
  see it.
- Do not treat a later `completed` audit row as playable DotA. Spawn, shops,
  items, and win conditions are all script-driven.

## Verification

There is no automated DotA scenario yet. After the load bugs, a dedicated run
must get past `jass_dobuffer` and `main()` without `SIGSEGV` or
`unimplemented native: InitHashtable`. Object-data work needs tests that a
map-scoped `Units\CampaignUnitFunc.txt` and `war3map.w3a` row win over base
TFT data. Hashtable natives need handle-lifetime tests, including
`FlushChildHashtable` and typed `Save*Handle` / `Load*Handle`.

See also [Campaign Map Audit](map-audit.md), [JASS Native Coverage](jass-native-coverage.md),
[WC3 Data Model](../../wc3-data-model.md), and [Loading and Assets](loading-and-assets.md).
