# Map Trigger Strings in Authored Names

## Contract

`TRIGSTR_<id>` is a map-local reference, not a displayable unit name. The
authoritative text is the matching `STRING <id>` entry in the active map's
`war3map.wts`. `G_MapString()` accepts only an exact `TRIGSTR_` token and
`G_LevelString()` applies that lookup through `level.mapinfo`.

## Data Flow

Map archive data flows through these owners:

```text
war3map.wts
  -> CM_ReadMapInfo / MAPINFO.strings
  -> G_MapString / G_LevelString
  -> WC3 unit, player, HUD, JASS, and entity snapshot consumers
```

Unit object overrides are a separate step. `war3map.w3u` can replace a
profile's `UnitProfile.Name` with a `TRIGSTR_` token while the profile row still
correctly retains that authored token. Consumers must call `G_UnitName()` (or
`G_LevelString()` for non-unit fields) at the presentation boundary.

The entity snapshot path is especially important: `G_CustomizeEntity()` packs
the resolved name into `CS_GENERAL`. The client receives that packed string,
but does not receive the server's `MAPINFO.strings`, so publishing the raw
token leaves `TRIGSTR_*` visible in the client UI.

## Human06 Evidence

In the configured Warcraft III campaign archive, `Maps/Campaign/Human06.w3m`
contains these authored references:

| Token | Map source | `war3map.wts` text |
|---|---|---|
| `TRIGSTR_028` | `war3map.w3u` unit name | `Plagued Male Villager` |
| `TRIGSTR_029` | `war3map.w3u` unit name | `Plagued Female Villager` |
| `TRIGSTR_241` | `war3map.w3i` player name | `Undead Elite Guard` |
| `TRIGSTR_242` | `war3map.w3i` player/force name | `Alliance Forces` |

These values are map-local. Do not add them to a global string table or replace
them with hardcoded names.

## Known Pitfalls

- A profile lookup returning `TRIGSTR_028` is correct raw data; resolving it
  while building display state is the required behavior.
- The renderer cannot resolve these tokens after the server has packed a raw
  value into `CS_GENERAL`; resolution must happen in the WC3 game module.
- Do not use installed retail archives in tests. The in-engine regression uses
  an in-memory profile row, map unit override, and WTS entry.

## Verification

The `wc3_slk.map_unit_name_resolves_wts_override` test verifies both the raw
profile value and the resolved `G_UnitName()` value, then drives
`globals.CustomizeEntity` and checks the packed configstring entry:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_slk.map_unit_name_resolves_wts_override'
make test-wc3-engine WC3_PATTERN='wc3_slk.*'
```

Run the WC3 engine suite in both ROC and TFT modes through the normal
`make test-wc3-engine` target.

See also [WC3 trigger-string syntax](file-docs/readme.md#trigstr-strings),
[loading and asset resolution](loading-and-assets.md), and
[server-authored UI payloads](../../architecture/ui-payloads.md).
