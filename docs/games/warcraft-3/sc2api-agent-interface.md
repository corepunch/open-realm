# Warcraft III SC2API Agent Interface

## Contract

OpenRealm's Warcraft III agent interface targets **SC2API Raw protocol compatibility**. The transport uses Blizzard's protobuf
`Request` / `Response` wire format over the `/sc2api` WebSocket endpoint, while Warcraft-specific behavior remains owned by
`games/warcraft-3/` and continues through the normal authoritative game/order code.

The compatibility layer must not become a second gameplay implementation. An external action decodes to a small
protocol-independent WC3 command, then uses the same order, target validation, resource, food, cooldown, pathing, build placement,
training, research, and spell paths as a normal player command.

The implementation is split into a protobuf-independent game core and a game-owned wire/transport edge. With `SC2API=1` and the
runtime `sc2_api` cvar enabled, OpenRealm now exposes a single-client `/sc2api` WebSocket endpoint, decodes the supported Blizzard
protobuf wire messages without requiring generated bindings, queues requests in receive order, and serializes matching responses.
The game core owns compatibility mappings, player/game metadata, player-filtered Raw observations, Raw fog/Blight map state,
SC2-style board coordinates, `StartRaw`, static data, terminal results, lifetime-safe unit tags, queries, and authoritative raw-command
translation. The generic server remains responsible for map transitions and actual simulation-frame advancement.

## Engine Boundary

`ARCHITECTURE.md` requires generic engine/server modules to stay game-agnostic. Therefore WC3-specific SC2API translation belongs
under `games/warcraft-3/`; do not add `#ifdef WC3` request handling to `server/`, `common/`, `client/`, or `renderer/`.

Keep protocol encoding at the transport edge:

```text
/sc2api WebSocket + SC2 protobuf wire messages
          |
          v
WC3 SC2API session / wire adapter
          |
          v
plain game-owned command/observation values
          |
          v
existing authoritative WC3 simulation
```

Normal gameplay code must not include generated `*.pb-c.h` files. The server/game interface therefore exposes only generic external-
controller hooks: queued map/quit requests, controller activity/clock ownership, an external-frame poll, server-owned step completion,
and map-transition completion. The server still calls the normal game `RunFrame` boundary for every requested simulation step.

## Build Flags And Optional Dependencies

Both features are off by default:

| Make variable | Default | Meaning |
|---|---:|---|
| `SC2API` | `0` | Compile the Warcraft III SC2API compatibility code. |
| `PROTOBUF` | `0` | Enable the optional `libprotobuf-c` dependency for the WC3 game module. |

The flags are independent. `SC2API=1 PROTOBUF=0` builds the complete supported SC2API endpoint using OpenRealm's small schema-compatible
wire codec, so protobuf is not a runtime dependency. `PROTOBUF=1` additionally opts the WC3 game module into `libprotobuf-c` for
schema/generated-binding validation and future tooling. A normal build with both flags at zero must not probe for or link protobuf.

Examples:

```sh
make
make SC2API=1 game
make SC2API=1 PROTOBUF=1 game
```

The generated protocol maintenance target additionally needs `protoc-c`, but ordinary builds do not:

```sh
make PROTOBUF=1 \
    SC2API_PROTO_DIR=/path/to/s2client-proto \
    wc3-sc2api-proto
```

`SC2API_PROTO_DIR` must be a local checkout or source export of Blizzard's `s2client-proto`. The build does not download it. The
preparation tool copies `s2clientprotocol/`, copies `PROTOCOL_LICENSE`, and extends only the `Race` enum in the generated copy.
Generated files live under `build/generated/wc3-sc2api/` and are not committed.

## Protocol Compatibility

The base schema is Blizzard's `s2client-proto` protocol. Existing Blizzard enum/field/message numbers must remain unchanged.
OpenRealm extends `SC2APIProtocol.Race` by appending Warcraft III values:

| Numeric value | Race |
|---:|---|
| `0` | `NoRace` |
| `1` | `Terran` |
| `2` | `Zerg` |
| `3` | `Protoss` |
| `4` | `Random` |
| `5` | `Human` |
| `6` | `Orc` |
| `7` | `Undead` |
| `8` | `NightElf` |

Values `0` through `4` are Blizzard values and must never be renumbered. `tools/prepare_sc2api_proto.py` appends values `5` through
`8` to a generated copy of upstream `common.proto`; it fails rather than silently editing an unexpected upstream Race definition.

An older SC2 client compiled against the original enum may treat values `5` through `8` as unknown enum values. An OpenRealm-aware
client generated from the prepared schemas receives the symbolic Warcraft race names.

## Resource Mapping

SC2API field names stay unchanged on the wire. Their Warcraft III meanings are:

| SC2API `PlayerCommon` field | OpenRealm Warcraft III value |
|---|---|
| `minerals` | lumber |
| `vespene` | gold |
| `food_cap` | food cap |
| `food_used` | food used |
| `food_army` | active owned non-worker unit food |
| `food_workers` | active owned worker food |
| `idle_worker_count` | owned workers currently eligible for the normal WC3 idle-worker shortcut |
| `army_count` | active owned non-worker, non-building unit count |

This is deliberately **minerals = lumber** and **vespene = gold**. Do not reverse the mapping in serializers, static unit costs, or
action errors.

The current game-owned helper `WC3_SC2API_FillPlayerCommon()` implements this mapping without depending on protobuf-generated types.
The worker/army split is derived from authoritative owned live units using the same worker capability and idle-worker predicate as the
normal WC3 shortcut system; buildings, dead units and units still in training are excluded. Agent-facing player arguments are Warcraft
player numbers, not raw `game.clients[]` indexes: map/client setup may bind a client slot to a different map-player number. `WC3_SC2API_PlayerClient()` performs the canonical lookup, and SC2API player ids are exposed one-based
(`Warcraft player number + 1`) to match the normal SC2API convention.

The game-owned static unit and upgrade adapters use the same mapping; the future protobuf serializer must copy it unchanged:

```text
SC2 mineral_cost -> WC3 lumber cost
SC2 vespene_cost -> WC3 gold cost
```

Similarly, SC2 action errors should eventually map WC3 lack-of-lumber to `NotEnoughMinerals` and lack-of-gold to
`NotEnoughVespene`.

## Coordinate Space

SC2 Raw clients assume a lower-left-origin game-board coordinate system whose dimensions match `StartRaw.map_size`. OpenRealm's
native Warcraft world can be centered around negative coordinates, so the protocol boundary must not expose native WC3 positions
directly.

The compatibility mapping is:

```text
1 SC2 board unit = 1 Warcraft pathing cell
                   = normally 32 WC3 world units

SC2 x = (WC3 x - world_bounds.min.x) / pathing_cell_size
SC2 y = (WC3 y - world_bounds.min.y) / pathing_cell_size
SC2 z = WC3 z / pathing_cell_size
```

`WC3_SC2API_WorldToBoardPoint()` / `WC3_SC2API_BoardToWorldPoint()` own this translation. Raw unit positions, unit radii, published
point-order targets, start locations and incoming `ActionRawUnitCommand.target_world_space_pos` all use that same board space. Unit
tags and unit-type ids are unaffected. This keeps ordinary SC2 tooling consistent with `StartRaw.map_size` and avoids leaking
OpenRealm's centered WC3 coordinate convention into the wire protocol.

## Stable Unit Tags

SC2API raw actions identify units with opaque `uint64` tags. OpenRealm must not expose a bare edict slot because an edict can be
reused after a unit dies.

The compatibility core keeps a game-local SC2 incarnation generation for each edict slot. The low 32 bits retain the edict number
for efficient lookup and the high 32 bits are an opaque SC2 generation. `spawn_time` is still tracked internally to detect an actual
OpenRealm edict-slot reuse, but it is not exposed as the SC2 generation itself.

`WC3_SC2API_ResolveUnitTag()` validates all of the following before returning an entity:

- non-zero tag;
- entity number still in the current edict range;
- edict still in use;
- stored `s.number` still matches the slot;
- current adapter generation still matches the tag.

An engine lifetime change (`spawn_time`) allocates a fresh SC2 generation. A Warcraft gameplay death also retires the SC2 generation
before corpse/revival processing. This extra boundary is required because WC3 can resurrect the **same** edict/JASS handle, while an
SC2 tag published in `Event.dead_units` is a terminal unit incarnation. A resurrected WC3 unit therefore reappears with a new opaque
SC2 tag without changing its Warcraft handle identity. The adapter generation table resets after a successful CreateGame/RestartGame map
load, so tag identity is scoped to one SC2 game episode.

Queued WC3 target orders continue to use their native `entity number + spawn_time` lifetime check. Treat the SC2 tag bit layout as an
implementation detail; external clients must treat tags as opaque identifiers.

Issued-order bookkeeping uses the same lifetime principle. `G_GetIssuedOrderTarget()` stores target entity number plus target
`spawn_time`, validates both before returning a target, and `G_InitEdict()` clears the out-of-edict issued-order tables whenever an
edict slot begins a new lifetime. Raw `Unit.orders[]` does **not** read that trigger-facing "last issued" state because Shift-queued
commands update it before becoming active. OpenRealm maintains a separate active-order snapshot and appends the authoritative
`unitOrderQueue_t` FIFO, so SC2 clients see the executing order followed by queued point/entity orders without changing JASS event
semantics. Stale queued entity targets are omitted rather than retargeting a reused edict slot.

## Unit Type Namespace And Destructables

Warcraft unit rawcodes and destructable rawcodes occupy separate object-data namespaces and can collide. SC2 Raw has one `uint32`
`unit_type` namespace, so ordinary WC3 units keep their rawcode unchanged while destructables use:

```text
SC2 destructable unit_type = WC3 destructable rawcode | 0x80000000
```

Standard Warcraft FOURCC bytes leave the high bit clear. This preserves existing unit ids while making neutral destructables
unambiguous to SC2 clients. Living visible destructables are exported as neutral Raw units with stable tags, position/radius, life and
`build_progress=1`; dead destructables leave the Raw unit list and participate in the normal one-shot death-event stream. Static
`UnitTypeData` enumerates loaded destructable rows but omits `ability_id`, because destructables are observation/target objects rather
than buildable SC2 unit types.

## Game Info And Observation Envelope

`WC3_SC2API_FillGameInfo()` provides the stable game-owned subset needed by `ResponseGameInfo` before protobuf serialization:

- resolved map display name;
- current local Warcraft map path;
- one-based participant ids;
- SC2-compatible `Participant=1` / `Computer=2` player types;
- requested/actual Warcraft race values `5-8`; Warcraft's `kPlayerRaceNone` map/lobby preference is exposed as SC2 `Random=4` for `race_requested`, while an unresolved actual race remains `NoRace=0`;
- resolved player names.

Neutral and rescuable Warcraft owners are simulation owners, not SC2API participants, so they are intentionally omitted from
`player_info`. Raw units may still report their owner numbers where applicable.

`WC3_SC2API_BuildObservation()` aggregates the authoritative `level.framenum`, `PlayerCommon`, and the player-filtered Raw unit list.
`level.framenum` is the game module's existing simulation-frame counter, so the future protobuf serializer can populate
`Observation.game_loop` without maintaining an agent-only clock.

`WC3_SC2API_FillPlayerResult()` maps players that have passed through the existing `RemovePlayer` result path to Blizzard's numeric
`Victory`, `Defeat`, `Tie`, and `Undecided` values. The future session layer remains responsible for deciding *when* SC2's
`ResponseObservation.player_result` is emitted; the game helper only translates an already-authoritative terminal player state.

### Static Raw Map / `StartRaw`

`WC3_SC2API_FillStartRaw()` now provides the game-owned shape corresponding to SC2 `StartRaw` using caller-owned buffers:

- `map_size`: Warcraft map width/height in SC2 board/pathing cells;
- `pathing_grid`: one bit per cell, `1` when normal ground movement is pathable;
- `placement_grid`: one bit per cell, `1` when the authored pathing flags permit ordinary building placement;
- `terrain_height`: one byte per cell using SC2's `[-200, 200] -> [0, 255]` board-height encoding;
- `playable_area`: W3I camera-bound complements converted from Warcraft terrain tiles into board cells;
- `start_locations`: human/computer W3I player start positions converted to board coordinates.

The pathing and placement grids are sampled from `CM_GetPathingFlagsAt()`, so this remains game/collision owned and does not infer
walkability from rendering. One-bit rows use the same MSB-first packing expected by normal SC2/PySC2 image unpacking.

### Raw Map State

`WC3_SC2API_FillMapState()` provides the game-owned subset corresponding to SC2 Raw `MapState` without introducing protobuf types.
It exposes two caller-owned `ImageData`-shaped buffers at the **same dimensions as `StartRaw.map_size`**:

- `visibility`: the represented player's authoritative WC3 fog state resampled onto the board/pathing grid, one byte per cell
  (`bits_per_pixel=8`), with SC2-compatible categorical values `0=Hidden`, `1=Fogged`, `2=Visible`;
- `creep`: a documented compatibility alias for Warcraft III **Blight**, resampled onto the same board grid and using SC2's one-bit
  image form (`bits_per_pixel=1`). Bits are packed most-significant-bit first within each byte so normal SC2/PySC2 image unpacking
  reads cells in index order.

The source fog grids already contain the normal Warcraft shared-vision result for connected viewers, so the adapter must not
recompute an agent-only vision model. Blight is global terrain state rather than player-private information and therefore does not
need a separate per-agent visibility filter. `WC3_SC2API_BuildObservationWithMapState()` composes this with the existing observation
envelope.

## Raw Observation Core

`WC3_SC2API_BuildRawUnits()` and `WC3_SC2API_FillRawUnit()` build protobuf-independent values corresponding to the stable subset of
`SC2APIProtocol.Unit` that OpenRealm can currently populate from authoritative WC3 state.

Observation construction is player-filtered. It must not walk `g_edicts` and expose every live entity directly. The current adapter
uses the existing WC3 fog/shared-vision/invisibility contracts:

- `G_FowPlayerCanSeeEntity()` decides whether an entity may appear at all. This already retains explored buildings as fog snapshots.
- `G_FowPlayerCanHoverEntity()` distinguishes actively visible/detected entities from explored snapshots.
- `globals.CustomizeEntity()` applies the same recipient-specific entity customization used by normal networking; entities still
  marked `RF_HIDDEN` after customization are omitted.
- `Visible` units may expose current health, mana (`energy`), build progress, cargo and other live state.
- `Snapshot` entries deliberately omit live combat/economy/order/buff/passenger fields so current fogged building state is not leaked.
- active WC3 status effects resolve through the same status-to-BuffID rule used by the normal HUD and are exported as deduplicated
  Raw `buff_ids`; expired status slots and status records that do not resolve to an authored buff id are omitted.
- self/allied transports expose SC2-shaped passenger tag, unit type, health and mana-as-energy records; enemy passenger lists and
  cargo occupancy/capacity remain redacted.
- issued orders are exposed only for `Self`/`Ally` observations; enemy orders are not exported.
- point-target orders retain their point; unit-target orders retain an opaque lifetime-safe target tag. If the target edict slot is
  later recycled, the accessor rejects the stale lifetime instead of silently targeting the replacement entity.

The adapter uses Blizzard's numeric `DisplayType`, `Alliance`, and `CloakState` values so protobuf serialization later requires no
enum reinterpretation. WC3 neutral-passive ownership maps to SC2 `Neutral`; hostile/non-allied owners map to `Enemy`.

The internal bot implementation is not an observation contract: it can access privileged simulation state and must not be used as
the external agent's visibility policy. Future query APIs must follow the same rule and must not reveal hidden/invisible entities
through side channels.

## Raw Actions

SC2 `ActionRawUnitCommand` carries:

```text
ability_id
unit_tags[]
queue_command
optional target_unit_tag
optional target_world_space_pos
```

The game-owned primitive `WC3_SC2API_IssueRawUnitCommand()` intentionally handles **one resolved source unit** at a time. It:

1. resolves the opaque unit tag and rejects stale lifetimes;
2. checks normal ownership/shared-control authority with `G_UnitCanControl()`;
3. treats `ability_id` as the stable WC3 order id and resolves it through `G_OrderId2String()`;
4. resolves unit targets only when the represented player can actively see/detect them;
5. converts SC2 board-space point targets back to Warcraft world coordinates;
6. enters `unit_issueimmediateorder()`, `G_IssueUnitPointOrder()`, or `G_IssueUnitTargetOrder()`;
7. therefore inherits existing WC3 queueing, targeting, spell/order validation and gameplay behavior.

The game-owned command primitive remains single-unit, while the wire adapter expands one SC2 `ActionRawUnitCommand` across every
`unit_tag` and aggregates the first non-successful per-unit result into the one `ResponseAction.result` entry associated with that
submitted SC2 `Action`. This keeps batching policy at the SC2 boundary while every unit still enters the normal authoritative WC3
order path.

The currently safe `ActionResult` subset uses Blizzard's existing numeric values for `Success`, `NotSupported`, `Error`,
`CantQueueThatOrder`, `CantTargetThatUnit`, `MustTargetVisibleUnit`, and `YouCantControlThatUnit`. More specific
resource/cooldown/target errors should be added only when the normal WC3 order path exposes a canonical failure reason that can be
translated without duplicating validation.

Immediate orders are currently rejected with `CantQueueThatOrder` when `queue_command` is requested because the existing WC3
immediate-order entry point does not represent a queued immediate command. Point/target orders pass the flag directly to the
existing Shift-order queue implementation.

Do not special-case movement, attack, build, train, research, resource payment, food, cooldowns, or spell costs inside the protocol
layer. The normal gameplay system remains authoritative. Broader build/train/research command coverage remains transport/data work,
not a second SC2API gameplay path.

## Static Unit Type Data

`WC3_SC2API_FillUnitTypeData()` exposes a protobuf-independent subset of SC2 `UnitTypeData` from OpenRealm's loaded Warcraft data.
`WC3_SC2API_BuildUnitTypeData()` enumerates the loaded base `UnitBalance` table plus map-created `war3map.w3u` unit ids, de-duplicating
ids while resolving each row through the normal map-overlay accessors. `WC3_SC2API_UnitTypeDataCapacity()` supplies a safe caller
allocation upper bound.
The resource mapping is the same one used everywhere else:

```text
SC2 mineral_cost = WC3 lumberCost
SC2 vespene_cost = WC3 goldCost
```

It also exposes the resolved/map-overridden unit name, transport cargo size, food required/provided, movement speed, sight range,
base armor, structure attribute, authored build time and up to two authored weapon records. Weapon target type maps WC3 ground/
structure and air target masks to SC2 Ground/Air/Any; damage uses the loaded WC3 average attack damage, range uses board units and
speed uses the authored attack cooldown. Spatial static fields use the same board scale as Raw positions: WC3 movement speed, weapon
range and sight range are divided by the pathing-cell world size before reaching the SC2-compatible data shape. `build_time` is the
WC3 authored build-time value in seconds.

`unit_type_id` currently uses the WC3 four-character unit rawcode as the stable numeric id. This is suitable for `RequestData` and
raw-unit observations without maintaining a duplicate agent-only unit database. External code must not interpret it as an SC2
`UNIT_TYPEID` enum value.

## Static Upgrade Data And Researched Upgrades

`WC3_SC2API_FillUpgradeData()` and `WC3_SC2API_BuildUpgradeData()` expose the game-owned subset corresponding to SC2 `UpgradeData`
from OpenRealm's loaded Warcraft upgrade table. The compatibility fields are:

```text
upgrade_id    = WC3 upgrade rawcode
name          = resolved WC3 object name
mineral_cost  = WC3 level-1 lumber cost
vespene_cost  = WC3 level-1 gold cost
research_time = WC3 authored level-1 research seconds
ability_id    = WC3 research/order rawcode
```

SC2 `UpgradeData` has one cost/time record, while Warcraft upgrades may have several authored levels with level-dependent modifiers.
The static compatibility record therefore describes the **first research level**. This is an explicit lossy mapping, not an
assumption that Warcraft upgrades are single-level.

The observation envelope also exports one upgrade rawcode for each player tech entry whose authoritative `researched` count is
greater than zero, corresponding to Raw `PlayerRaw.upgrade_ids`. Warcraft research levels greater than one remain one present id
because SC2's field is a repeated id list rather than an id-to-level map. In-progress-only research is not reported as researched.

`WC3_SC2API_FillBuffData()` / `WC3_SC2API_BuildBuffData()` expose the corresponding static SC2 `BuffData` subset using the WC3
buff rawcode as `buff_id` and the loaded/resolved Warcraft buff tooltip as `name`. This keeps observed `buff_ids` and static buff
metadata in the same id space without an agent-only registry.

## Step Semantics

In non-realtime sessions SC2 `RequestStep.count` is implemented as a count of authoritative OpenRealm WC3 simulation frames. The WC3
module requests a generic external-control frame budget and the server remains the owner of `RunFrame`; the module never advances a
second private clock. A step batch stops early if the represented game reaches a terminal result, and `ResponseStep.simulation_loop`
reports the resulting authoritative `level.framenum`.

Because dedicated execution does not initialize the client renderer/audio/HUD, repeated Step requests also provide the current
headless faster-than-realtime path without coupling simulation stepping to renderer timing.

## Protocol Preparation

`tools/prepare_sc2api_proto.py` intentionally has no downloader. Given a local upstream source tree it:

1. verifies `s2clientprotocol/common.proto` exists;
2. copies the complete upstream `s2clientprotocol/` tree to the generated build directory;
3. preserves Blizzard Race values `0-4` and appends `Human=5`, `Orc=6`, `Undead=7`, `NightElf=8`;
4. copies upstream `PROTOCOL_LICENSE` alongside the generated schema tree;
5. refuses a partial or unexpected Race layout rather than guessing.

`make wc3-sc2api-proto` then runs `protoc-c` against the generated schemas. The generated C bindings are build products and are not
required by the runtime endpoint; the hand-written edge codec intentionally covers only the supported SC2 messages and preserves
protobuf field/wire compatibility while keeping `libprotobuf-c` optional.

Upstream protocol source: <https://github.com/Blizzard/s2client-proto>

## Runtime Endpoint And Session Model

The endpoint is compiled only with `SC2API=1` and is disabled at runtime by default. Enable it with:

```sh
openwarcraft3 -data /path/to/Warcraft3 +dedicated 1 +sc2_api 1
```

When no `+map` is supplied, dedicated startup is allowed only when an active generic external controller exists. The controller may
then send `RequestCreateGame`; map resolution/loading is queued back to the generic server so the game module never re-enters server
teardown from inside an export callback. Defaults are `sc2_api_listen=127.0.0.1` and `sc2_api_port=5000`, serving WebSocket path
`/sc2api`. One external connection is supported at a time.

The supported state flow follows Blizzard's core single-player SC2API sequence:

```text
launched --CreateGame--> init_game --JoinGame--> in_game --terminal--> ended
   ^                           |                    |                   |
   |                           |                    +--LeaveGame-------+
   +---------------------------+-------------------- CreateGame/Restart
```

Requests are copied into a bounded FIFO and processed in receive order. An asynchronous Create/Restart/Step/realtime-Observation
request blocks later requests until its matching response is ready. An explicitly present request id, including id `0`, is echoed on
the response. Queue exhaustion closes/reinitializes the transport rather than silently violating request/response ordering.

In non-realtime mode the external controller owns the simulation clock. `RequestStep(count)` yields an exact server-owned frame budget;
the server calls the ordinary `RunFrame` boundary for those frames and then sends `ResponseStep.simulation_loop`. Dedicated mode has no
client renderer/audio/UI, so repeated Step requests also provide the initial headless faster-than-realtime execution path. In realtime
mode, `RequestObservation.game_loop` waits until the requested authoritative loop has been reached.

## Query Pathing

`RequestQuery.pathing` is answered synchronously from the generic OpenRealm routing backend. Point-start queries use a zero-radius ground
mover. Unit-start queries use the visible unit's current WC3 position, collision radius and ground/flying pathing class; a remembered tag
for a unit that is no longer actively visible returns distance `0` rather than becoming a fog-of-war position oracle. The search uses the
same immutable/static pathing footprint expansion and diagonal corner restrictions as unit routing. Dynamic unit-to-unit collision is not
folded into this static route distance. As in SC2, `0` means no path exists. Returned world distance is converted to the established SC2
board/pathing-cell coordinate scale.

## Supported Wire Requests

The runtime wire adapter currently supports:

- `RequestCreateGame` with local `map_path`, player-setup presence, optional random seed and realtime mode; inline `map_data`, Battle.net
  maps and `disable_fog=true` are rejected rather than silently approximated; map-authored Warcraft player setup remains authoritative;
- `RequestJoinGame` for the Raw interface, including participant selection by Warcraft race and observer perspective by player id;
  feature/render/score/cropped/raw-cheat visibility options are rejected when requested;
- `RequestRestartGame`, `RequestLeaveGame`, `RequestQuit`, `RequestPing`;
- `RequestGameInfo`, including `StartRaw` when Raw is enabled;
- `RequestObservation`, including `PlayerCommon`, Raw units/PlayerRaw/MapState, game loop, one-shot visible/owned/allied
  `raw_data.event.dead_units`, and one-shot terminal `PlayerResult`;
- `RequestAction` Raw unit commands, including multi-tag commands aggregated to one SC2 `ActionResult` per submitted `Action`,
  Raw camera movement as session-local `PlayerRaw.camera`, and Raw autocast toggling through the existing WC3 autocast subsystem;
- `RequestStep`;
- `RequestData` for abilities, unit types, upgrades and buffs; EffectData is explicitly reported unsupported;
- `RequestQuery` available abilities, static-routing path distance and build placement; `AvailableAbility.requires_point` is populated for
  point-only commands and building placement. Worker build submenus are flattened into their concrete WC3 building rawcodes for SC2
  availability queries. `ignore_resource_requirements=true` follows Blizzard's query contract: gold/lumber/food affordability and cooldowns
  are ignored, while mana/energy, tech prerequisites, ownership and target/placement validity remain enforced;
- `RequestAvailableMaps`, populated from the engine filesystem map enumerator.

Unsupported SC2 request kinds (replays, quick-save/load, save-map/replay, UI/feature/render camera operations, debug and map-command) return a normal
SC2 response envelope with `Response.error` rather than being interpreted as another operation.

## Current Coverage

Implemented:

- default-off `SC2API` and independent default-off `PROTOBUF` build flags;
- no protobuf lookup/linking for normal builds, and no protobuf runtime requirement for `SC2API=1 PROTOBUF=0`;
- reproducible local upstream schema preparation, preserving Blizzard values and appending Warcraft Race values `5-8`;
- RFC 6455 binary WebSocket endpoint at `/sc2api`, request FIFO, request-id echo and matching response envelopes/status values;
- core create/join/restart/leave/quit lifecycle and server-owned realtime/stepped clocks;
- headless dedicated startup without a preselected map when SC2API owns map creation;
- `minerals=lumber`, `vespene=gold` player resources, static costs and resource action errors;
- one-based external player ids resolved from Warcraft player numbers rather than client-array indexes;
- GameInfo, StartRaw, observation/Raw/MapState, terminal result, Data, Query and AvailableMaps serialization;
- full-map static path-distance queries using the same footprint expansion, ground/flying pathing flags and diagonal corner rules as OpenRealm routing;
- researched upgrades, loaded unit/upgrade/buff metadata and richer AbilityData target/autocast/range metadata;
- UnitTypeData structure/armor/build-time/weapon records in addition to costs, food, movement and sight;
- lifetime-safe opaque unit and target tags;
- coherent SC2 board coordinates for observations, point commands, placement queries and start locations;
- player fog/shared-vision/invisibility filtering, snapshot redaction and Blight-to-creep mapping;
- friendly unit health/mana, active-plus-Shift-queued Raw orders, buffs, cargo/passengers, rally targets and attack/armor upgrade levels
  with SC2-compatible enemy redaction;
- neutral living Warcraft destructables as Raw units plus collision-free namespaced `UnitTypeData`, and one-shot Raw death events for
  deaths the represented player was entitled to observe;
- SC2 `Attribute.Heroic` on WC3 Hero unit types;
- authoritative immediate/point/unit-target orders plus build/train/research/building-upgrade routing through existing WC3 systems;
- Raw autocast toggle actions and a session-local Raw camera round-tripped through `PlayerRaw.camera` without touching the client HUD;
- multi-tag raw command execution with one response result per submitted SC2 Action;
- resource/food/tech/placement and common target/control error mapping where the existing WC3 API exposes a definite reason.

Deliberately unsupported or incomplete rather than guessed:

- SC2 Feature Layer / Render / UI interfaces;
- replay, quick-save/load, debug, map-command and save-map/replay protocols;
- SC2 EffectData and transient effect observations;
- spell-aware Shift queues that OpenRealm itself does not yet represent in `unitOrderQueue_t`;
- SC2 Raw fields without a direct authoritative WC3 source such as sensor/radar rings, shield values and weapon cooldown snapshots;
- richer Warcraft-only concepts without a direct SC2 field, including full inventory/item state, persistent corpse state, day/night state
  and dynamic hero progression (level/XP/attributes/skill points);
- exact mutation of Warcraft map-authored lobby/player setup from SC2 `PlayerSetup` records; the records are required for CreateGame but the
  loaded WC3 map's authored slots remain authoritative.

These omissions are not prerequisites for an external Raw SC2API client to create/load a Warcraft map, join, observe, issue supported
unit commands and drive a stepped headless simulation.

## Verification

The compatibility tests are compiled only when both `BZ_TESTS` and `WC3_SC2API` are defined. They cover:

- Warcraft race mapping uses numeric values `5-8` without changing SC2 values `0-4`;
- `PlayerCommon.minerals` receives lumber and `PlayerCommon.vespene` receives gold;
- `PlayerCommon` worker/army food and counts are derived from the authoritative owned unit set and normal idle-worker predicate;
- external player ids are one-based and player lookup does not assume client-index identity;
- game info contains human/computer participants with Warcraft race ids and omits neutral owner slots;
- observation game loop comes from `level.framenum`;
- researched tech entries appear once in Raw `upgrade_ids`, while in-progress-only research does not;
- removed-player result values translate to SC2 result numbers;
- stale unit tags fail after an entity lifetime stamp changes, and a gameplay death retires the SC2 incarnation so same-handle WC3 resurrection returns under a fresh tag;
- static unit costs preserve `minerals=lumber` and `vespene=gold`, and unit data exports structure/heroic/armor/build-time/weapon shape;
- static upgrade data uses level-one WC3 lumber/gold/research-time values with the established resource mapping;
- static buff data uses loaded WC3 buff rawcodes and names;
- loaded unit-type enumeration includes normal WC3 rows;
- basic raw-unit observations use stable tags and live authoritative fields;
- visible active status effects export deduplicated authored buff ids while expired statuses are omitted;
- self/allied transport observations expose passenger identity/type/health/energy detail;
- producer rally targets use SC2 board coordinates and lifetime-safe entity tags when applicable;
- enemy observations do not expose orders, passenger lists or cargo occupancy/capacity;
- friendly Raw orders expose the currently active point/entity command followed by Shift-queued FIFO entries, with lifetime-safe queued targets;
- living destructables appear as neutral Raw units with namespaced type ids;
- Raw map state maps unexplored/fogged/visible WC3 cells to `0/1/2` on the StartRaw board grid and packs Blight into the one-bit SC2 `creep` image order;
- board/world coordinate conversion is reversible at pathing-cell scale;
- `StartRaw` exports coherent map dimensions, MSB-packed pathing/placement, terrain height, playable area and start locations;
- synchronous path-distance routing returns direct distance on open ground, follows static detours and returns zero for unreachable endpoints;
- point-target raw actions convert SC2 board coordinates back to the expected WC3 world target;
- a friendly target order exports a target unit tag and rejects it after the target lifetime stamp changes;
- a single-unit raw immediate command routes through the existing WC3 order path and reports the compatible action result.

Per the requested workflow, this patch was prepared without compiling or running tests locally. Verification should include an
`SC2API=1 PROTOBUF=0` WC3 test build and the `wc3_sc2api.*` suite. The external smoke test should additionally verify one-shot
`Event.dead_units` delivery/visibility filtering and destructable `UnitTypeData`, followed by a real WebSocket/protobuf check covering Ping,
CreateGame, JoinGame, GameInfo, Observation, Action and Step. Protobuf is required only when separately validating the optional
`PROTOBUF=1` generated-schema/tooling path.
