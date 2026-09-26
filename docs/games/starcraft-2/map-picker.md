# StarCraft II map catalog and terminal picker

## Contract

`tools/parity/sc2.sh` launches OpenRealm with a selected installed map. `--list` prints the catalog, `--select` opens the searchable terminal picker, and `--map=slug` resolves a listed alias. An explicit `.SC2Map` path is passed through. `SC2DATA` names the installation and `SC2_DRY_RUN=1` prints the command without launching it. The default alias is `traynor01`.

The picker and its search, keyboard handling, colors, and compact layout live in `tools/parity/map_picker.py`; shared MPQ member reads live in `map_archive.py`. Warcraft III's `wc3_maps.py` and StarCraft II's `sc2_maps.py` supply their own rows. This extraction came from [PR #504](https://github.com/corepunch/open-realm/pull/504), which introduced the Warcraft III picker. Future game catalogs can reuse the same row fields: `edition`, `slug`, `name`, `title`, `campaign`, and `path`.

## Data flow

`sc2_maps.py` finds `.SC2Maps` archives below the installation and reads each archive's `(listfile)` through `mpqtool`. A `Maps/.../*.SC2Map/MapInfo` entry establishes the map path. `enUS.SC2Data/LocalizedData/GameStrings.txt` supplies the authored `DocInfo/Name`. Loose `.SC2Map` and `.SC2Components` map directories or archive files below the installation are included when they provide `MapInfo` or `MapInfo.xml`. Missing names are reported on stderr and displayed by their map identifier. Missing archive listfiles fail explicitly because the catalog cannot establish which maps exist.

The observed local `data/StarCraft2/Campaigns/Liberty.SC2Campaign/Base.SC2Maps` contains five map paths. Its `TRaynor01.SC2Map` resolves to `DocInfo/Name=Liberation Day`. Other installations follow their own archive contents; the catalog does not maintain a hardcoded mission list.

`build/parity/sc2-maps.json` caches rows using archive and loose-map signatures. `--refresh` forces regeneration. `SC2_MAP_CATALOG` can point to a fixture catalog with a `maps` array for command testing. Duplicate slugs or archive paths fail rather than picking an arbitrary map. A loose map with the same path as an archive map is reported and omitted because the engine searches archives first.

## Diagnostic workflow

```sh
make mpqtool opensc2
SC2DATA=data/StarCraft2 tools/parity/sc2.sh --list
SC2DATA=data/StarCraft2 tools/parity/sc2.sh --select
SC2DATA=data/StarCraft2 SC2_DRY_RUN=1 tools/parity/sc2.sh --map=traynor01
make test-sc2-parity
```

The launcher records the command, Git state, and process output under `build/parity/logs/`. It launches OpenRealm; launching the maps nested inside `.SC2Maps` archives with the retail client has not been verified. `--select` needs an interactive terminal; `--list` works in scripts.

External loose-map directories are not offered by the picker. A bounded engine launch with a `../` map path reported `Can't find map` before map initialization, so advertising such a destination would produce a dead selection. Place loose maps below `SC2DATA/Maps` for this workflow.

The installation's Battle.net cache contains hashed `.s2ma` files. At least one observed file was not readable by `mpqtool` as a map archive, so the catalog does not scan that cache as loose maps. An explicit `.s2ma` path can still be passed to the launcher for separate diagnosis.

See also [the parity harness](../../../tools/parity/README.md) and [SC2 map loading](../../fs-loading-architecture.md).
