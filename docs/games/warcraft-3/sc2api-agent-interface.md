# Warcraft III SC2API Raw agent interface

The optional Warcraft III game module endpoint accepts Blizzard SC2API protobuf messages over one
loopback WebSocket at `ws://127.0.0.1:5000/sc2api`. It translates requests into existing Warcraft
orders, production, research, autocast, visibility and routing; it does not run another simulation.
The implementation adapts sookyboo's original [PR #506](https://github.com/corepunch/open-realm/pull/506),
commit `b1fc460aed42ba653918e242856f2e5c8a2d08ed`, to the current ability and pooled-state APIs.
The integration in [PR #653](https://github.com/corepunch/open-realm/pull/653) retains that original
commit in its ancestry while using the adapted implementation. Do not reapply the WIP patch: its
inline entity state and ability hooks predate the current APIs; all 32 original core tests remain.

## Build and start

| Configuration | Result |
|---|---|
| `SC2API=0 PROTOBUF=0` (default) | Normal build; no protobuf lookup/link, endpoint or adapter runtime. |
| `SC2API=1 PROTOBUF=0` | Raw endpoint with a bounded, built-in protobuf wire codec. |
| `SC2API=1 PROTOBUF=1` | Same endpoint plus optional `libprotobuf-c` linkage and schema tooling. |

```sh
make SC2API=1 PROTOBUF=0 openwarcraft3
build/bin/openwarcraft3 -data /path/to/Warcraft3 +dedicated 1 \
  +set sc2_api 1 +set sc2_api_port 5000 +com_maxfps 0
```

Use `+set` for these optional startup cvars: they are not registered in the ordinary engine cvar
catalog. `sc2_api` defaults to `0`; `sc2_api_listen` defaults to `127.0.0.1`; the port defaults to
`5000`. No initial map is needed when an external controller is enabled. Dedicated mode creates no
renderer/audio window. `com_maxfps=0` removes the host-loop cap; manual Step then runs faster than
wall-clock simulation without changing the authoritative frame interval.

The game-library build fingerprint changes with `SC2API`/`PROTOBUF`, so switching flags cannot reuse
a library built with the previous optional configuration. Generated bindings are build products,
not committed source or required runtime dependencies:

```sh
make PROTOBUF=1 SC2API_PROTO_DIR=/path/to/s2client-proto wc3-sc2api-proto
```

This explicitly requires `libprotobuf-c`, `pkg-config` and `protoc-c`. It copies a local upstream
schema tree and `PROTOCOL_LICENSE`, appends Warcraft race values, then generates C bindings under
`build/generated/wc3-sc2api/`. It never downloads dependencies during a build.

## Ownership and lifecycle

`sc2api_compat`, `data`, `map`, `raw` and `game` use plain game-owned values. `sc2api_wire`, `transport`
and `server` implement the protobuf/WebSocket edge. No generated protocol structure or Warcraft
translation belongs in `common/`, `client/`, `renderer/` or `server/`.

The generic server owns map loading, fixed-step timing and `RunFrame`. Optional generic imports
`RequestMap`, `RequestQuit` and `ListMaps` and exports `External*` carry external-control requests.
Map transitions run after the game export returns; completion uses the current export table rather
than retaining a callback across `SV_Map`. Current game modules are linked and initialized through
`GetGameAPI`; there is no separately versioned runtime game-module loader. Engine and game libraries
must be rebuilt together after this in-process import/export layout change. The ordinary network
wire format and save layout do not change.

The endpoint/session has process lifetime, independently of a map's `G_ShutdownGame`: a failed map
load frees game entities and clients but retains the connection to report failure and accept a fresh
CreateGame. Completion callbacks after failure touch session state only. `ServerShutdown` explicitly
releases protocol buffers, snapshots and sockets in tests; OS process shutdown closes the production
endpoint. Socket state starts at the platform invalid descriptor, including before initialization.

Supported lifecycle:

```text
launched --CreateGame--> init_game --JoinGame--> in_game --terminal--> ended
    ^                                           |
    +-------------------LeaveGame---------------+
```

Create is allowed from launched/ended, Restart from ended, and actions/Step only while joined and
in_game. Invalid transitions and unsupported requests return protocol errors. Realtime simulation
also waits for Join and freezes after terminal/failure; manual mode advances only a requested frame
budget. Large Step batches are serviced in at most 256-frame chunks, retaining request order and
stopping early on terminal results. Realtime Observation can wait for a requested future loop.

Create accepts local map paths, optional random seed and realtime. Inline map data, Battle.net maps
and fog-cheat options are rejected. PlayerSetup is bounded and matched to the loaded W3I's authored
human/computer slots and races; it cannot rewrite campaign player numbers, controllers, starts or
alliances. Unsupported SC2 AI difficulty/build settings are rejected. Join selects a compatible
human slot, or an authored participant's observer perspective. Join registers the represented
player through `G_FowConnectPlayer` and the normal authoritative fog update.

## Compatibility mappings

| SC2 value | Warcraft value |
|---|---|
| `PlayerCommon.minerals`, unit/upgrade `mineral_cost` | lumber |
| `PlayerCommon.vespene`, unit/upgrade `vespene_cost` | gold |
| `Unit.energy` / `energy_max` | mana / maximum mana |
| `MapState.creep` | Blight |
| `unit_type` | unit rawcode; destructible rawcode with high bit `0x80000000` set |
| `UpgradeData.upgrade_id` / `ability_id` | upgrade/research rawcode |
| `Unit.buff_ids` / `BuffData.buff_id` | authored Warcraft BuffID rawcode |

Blizzard Race values remain `NoRace=0, Terran=1, Zerg=2, Protoss=3, Random=4`; extensions append
`Human=5, Orc=6, Undead=7, NightElf=8`. Older proto2 clients retain unknown enum fields on the wire;
Warcraft-aware clients can use the prepared schema's symbolic names. Neutral/rescuable owners do
not become GameInfo participants. External player IDs are Warcraft player numbers plus one;
`WC3_SC2API_PlayerClient` resolves them without assuming a matching client-array index.

Board coordinates use a lower-left origin: `(world - map_min) / pathing_cell_world_size`, normally
32 world units per board unit. Positions, point actions, radii, sight/weapon ranges, movement speed,
starts, playable bounds and returned route distances share this conversion. Z is scaled by the same
cell size. `game_loop` is the authoritative `level.framenum`, not day/night or a private agent clock.

## Observations, visibility and identity

Raw includes PlayerCommon food/worker/army totals, PlayerRaw upgrades/camera, units, map state,
known death events and terminal results. Worker classification uses `G_UnitIsWorker` and the normal
idle-worker predicate. Completed researched IDs appear once even for multilevel upgrades.

`G_FowPlayerCanSeeEntity`, `G_FowPlayerCanHoverEntity` and normal recipient customization govern
visibility/shared vision/invisibility/detection/hidden state. Enemy orders, cargo and passengers are
redacted. Self/allied visible cargo uses the real capacity and passenger registry. Training edicts
and persistent corpses are not published as living units.

Observed buildings/destructibles retain a bounded per-viewer snapshot of public type/owner/position/
radius/tag fields. Fog snapshots never read current life, mana, orders, buffs or ownership. Unseen
destruction, morphing or edict reuse cannot silently remove/update a remembered snapshot; its last
known position must become visible before it is retired. Step captures intermediate building
visibility so a long batch does not discard a sighting between observations. Newly constructed
unseen buildings do not appear merely because their cell was explored earlier.

A tag's low 32 bits identify a slot and high 32 bits identify an adapter incarnation. Spawn-time
checks reject recycled slots. Gameplay death retires the incarnation before corpse handling, so
same-handle Warcraft resurrection gets a new SC2 tag. Episodes reset the adapter table; Warcraft/
JASS handle semantics are unchanged. Known deaths require ownership or current/shared visibility;
alliance alone is insufficient. Death events and terminal results are consumed only after their
observation is accepted by transport.

Orders expose the active accepted command followed by the real `unitOrderQueue_t` FIFO, including
lifetime-checked targets and queued Build rawcodes. The trigger-facing last-issued-order tables are
separate. Friendly producer orders use the normal training/research FIFO and progress. Active-order
tracking exists only in SC2API builds. Spell-aware Shift queues and Warcraft Hero revival/progression
extensions remain unsupported. Point rallies are published; entity rallies require active target
visibility. Untouched default SELF rallies are omitted, preventing a hidden target's automatic
invalidation from becoming a death/position side channel.

Static StartRaw grids sample immutable terrain pathing, without current unit/building blockers.
Pathing/placement and Blight images are MSB-first one-bit images; terrain height uses one byte per
cell and the SC2 `[-200,200]` board-height encoding. Visibility uses `0=Hidden, 1=Fogged, 2=Visible` at
the same map dimensions, resampling the existing player fog grid.

## Actions and queries

Raw unit commands support immediate, point and unit targets, multiple source tags and the native
Shift FIFO. One SC2 Action returns one aggregate result while each source goes through normal
Warcraft order validation. Basic orders use Warcraft order IDs; command-card abilities use authored
ability rawcodes; construction/training/building upgrades use unit rawcodes; research uses upgrade
rawcodes. Alias-aware command resolution retains the unit's concrete authored ability. Unsupported
commands/queue forms return failure rather than invented success.

Resource-spending commands require `G_UnitCanSpendResources`, including advanced shared control for
allied producers. Cost/food/tech validation and payment use the producer owner's state; order events
retain the actual issuer. Basic shared control still allows ordinary army commands. Raw autocast
uses the existing ability autocast state. Raw camera moves are session-local and do not move the
human renderer camera or simulation units.

Queries provide normal available commands, concrete worker build choices, placement validation and
static route distance. `ignore_resource_requirements` bypasses gold/lumber/food affordability and
cooldown, retaining mana, research/ability requirements, disabling statuses and construction gates.
The shared `S_SpellCanActivate` policy serves normal casts and read-only spell queries; queries never
change resources/cooldowns to simulate affordability.

A supplied stale builder tag is rejected. Placement requires a currently visible footprint before
consulting dynamic blockers, so it cannot probe hidden enemy positions. Unit-start path queries
require current visibility/detection and use that unit's radius and movement flags. Routing obeys
static ground/flight blockers and diagonal corner rules and returns zero for no path. It uses
independent transient search arrays and restores normal movement-search state; no incremental
heatmap job is consumed or replaced.

## Protocol and deliberately unsupported features

Implemented requests: Ping, AvailableMaps, CreateGame, JoinGame, RestartGame, LeaveGame, Quit,
GameInfo, Data (abilities/unit types/upgrades/buffs), Observation, Action, Step and Query. The transport
validates client masks, reserved/control-frame bits and message/queue bounds; supports binary
fragmentation, ping/pong and clean close; preserves request FIFO and explicit IDs including zero;
and atomically queues complete response frames under backpressure.

Constants/field numbers follow Blizzard's [s2client-proto](https://github.com/Blizzard/s2client-proto),
verified against commit `4867bb9ede01e914c57a6fc9575dcc2f4bf0ca5a` (`sc2api.proto`, `raw.proto`,
`common.proto`, `query.proto`, `data.proto`, `error.proto`). The handwritten codec accepts stock
proto2 messages; it does not expose internal `edict_t` or snapshot structures on the wire.

Unit data enumerates base/custom units and destructibles, with costs, food, armor, build time,
weapons, sight, movement and stock Structure/Heroic attributes. Ability data includes registered
orders and concrete base/custom command-card abilities. Upgrades expose level-one cost/time because
SC2 has only one tuple; researched levels greater than one still appear once in `upgrade_ids`.
Buffs resolve through the same extracted `G_UnitStatusBuffCode` helper used by the normal HUD.

Feature Layer, RGB Render/UI actions, replays/save-map/quick-save/debug/map-command, EffectData and
renderer/missile/impact effects are explicitly unsupported. Warcraft inventory/items, persistent
corpses, dynamic Hero progression and day/night need explicit extensions; unrelated stock SC2 fields
are not repurposed. Semantic persistent area effects require a future game-owned whitelist.

## Verification workflow

```sh
make test                                      # clean ordinary baseline and final ordinary suite
make SC2API=1 PROTOBUF=0 openwarcraft3 openwarcraft3-tests
make SC2API=1 PROTOBUF=0 WC3_PATTERN='wc3_sc2api*' test-wc3-engine
make SC2API=1 PROTOBUF=0 test-server-net test-sc2api-transport
python3 tools/engine_boundary_audit.py
```

`tests/test_sc2api_transport.py` uses Python's standard library and generated test assets to exercise
a real dedicated endpoint, fragmentation/ping/close/masking/FIFO, lifecycle, recovery after a real
map-load failure and exact stepping.
`games/warcraft-3/game/tests/t_sc2api*.c` exercises the production core and wire/session dispatcher;
only response delivery is captured. The generic server suite verifies server-owned frame budgets
and early terminal completion. CI explicitly builds/tests SC2API without protobuf in debug/release,
in addition to the normal default jobs.

An independent generated-client check is optional and requires developer Python protobuf tooling:

```sh
python -m grpc_tools.protoc -I/path/to/s2client-proto --python_out=/tmp/sc2proto \
  /path/to/s2client-proto/s2clientprotocol/*.proto
python tests/test_sc2api_transport.py --proto-python /tmp/sc2proto
```

This uses unmodified official Python bindings for Ping, maps, Create/Join, GameInfo/Data, Raw
observations/actions, abilities/pathing queries, Step, Leave and Quit. It is not required to build or
run the endpoint. `PROTOBUF=1` C tooling needs a separate installed protobuf-c environment.

The issue-509 verification ran the full cross-game suite in default and `SC2API=1 PROTOBUF=0`
configurations, plus debug/release focused adapter tests and the real endpoint harness. The optional
`SC2API=1 PROTOBUF=1` release build and ROC/TFT adapter suites were also checked with the actual
protobuf-c 1.5.2 runtime. Independent official generated Python bindings exercised the complete
request cycle above. The C-binding maintenance target was not run because `protoc-c` was unavailable;
its generated files are not used by the endpoint.
