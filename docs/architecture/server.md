# Server Architecture

The server (`server/`) is the authoritative simulation layer. It owns the canonical game state, runs the selected game library as a shared library, and delivers entity snapshots to clients each frame. For the default Warcraft III build, game logic lives in `games/warcraft-3/game/` and is built as `libgame`.

## Startup

`SV_Init` (`server/sv_main.c`) runs at process startup:

1. Loads the game library (`libgame.so` / `game.dylib`) via `dlopen` and obtains a `game_export_t *` pointer by calling `GetGameAPI`.
2. Calls `ge->Init()` to let the game library set up its global state.
3. Calls `SV_InitGame` which reads the map archive, spawns world entities (doodads, pre-placed units, starting locations), and notifies the game library that a new map has loaded.

## Main Loop

`SV_Frame(DWORD msec)` is called from the platform main loop alongside `CL_Frame`. Unlike the client, the server advances only when enough real time has elapsed to cover a fixed `FRAMETIME` (100 ms):

```c
void SV_Frame(DWORD msec) {
    svs.realtime += msec;
    if (svs.realtime < sv.time)
        return;          // not yet time for a new game frame
    SV_ReadPackets();    // 1. process client commands
    SV_RunGameFrame();   // 2. advance simulation
    SV_SendClientMessages(); // 3. send snapshots to clients
}
```

This fixed-step approach decouples the simulation rate from the render rate and makes replay and deterministic simulation practical.

### 1. SV_ReadPackets

Drains the network receive buffer (loopback ring or UDP socket) for each connected client slot. Dispatches `clc_*` messages:

| Opcode | Effect |
|--------|--------|
| `clc_connect` | Allocate a client slot, send `svc_serverdata` + baselines + UI layout |
| `clc_move` | Store the `usercmd_t` for this client for use in `ge->ClientThink` |
| `clc_stringcmd` | Forward console commands to `SV_ExecuteClientCommand` |

### 2. SV_RunGameFrame

```c
void SV_RunGameFrame(void) {
    sv.framenum++;
    sv.time += FRAMETIME;
    ge->RunFrame();   // runs the game library's g_main.c G_RunFrame
}
```

`G_RunFrame` (in `games/warcraft-3/game/g_main.c`) iterates every live entity and calls `G_RunEntity`, which dispatches on `movetype` and calls the entity's `think` or `update` callback.

### 3. SV_SendClientMessages

For each connected client, `SV_BuildClientFrame` (`server/sv_ents.c`) collects the entities visible to that client into a snapshot. `SV_WriteFrameToClient` then delta-encodes the snapshot against the previous one using `MSG_WriteDeltaEntity` and transmits it as an `svc_packetentities` message.

Delta compression ensures only changed entity fields are sent, keeping bandwidth usage low even with many active entities.

## Game Library Interface

The server communicates with the game library through two vtable structs defined in the selected game's API directory. For Warcraft III, that is `games/warcraft-3/game/api/`.

### `game_import_t` — server services provided to the game

The server fills this struct and passes it to `GetGameAPI`. It provides:

| Function | Purpose |
|----------|---------|
| `LinkEntity` / `UnlinkEntity` | Register an entity with the spatial index |
| `BoxEdicts` | Spatial query — all entities inside a bounding box |
| `Trace` | Sweep test against world geometry |
| `PointContents` | Terrain/water flags at a world position |
| `SetModel` | Assign a model by name to an entity |
| `MemAlloc` / `MemFree` | Server heap |
| `WriteXxx` | Network message write helpers |

### `game_export_t` — entry points exported by the game library

| Function | Purpose |
|----------|---------|
| `Init` | Called once at server startup |
| `Shutdown` | Called when the server shuts down |
| `SpawnEntities` | Parse map entities and place initial units |
| `RunFrame` | Advance simulation by one `FRAMETIME` tick |
| `ClientConnect` | Called when a new client slot is allocated |
| `ClientBegin` | Called when a client finishes loading the map |
| `ClientThink` | Per-frame input processing for each client |
| `ClientDisconnect` | Client disconnected — clean up player entity |

## Entity System

Entities are fixed-size `edict_t` structs stored in a flat array (`g_edicts[]`) in the game library. The server and game library share a read-only view of this array via `ge->edicts` and `ge->num_edicts`.

Each entity has:
- `entityState_t s` — the fields transmitted to clients (origin, angles, model index, frame, effects flags, …)
- `entityShared_t shared` — spatial data used by the server for link/unlink/trace (bounding box, link count, …)
- All other fields (health, AI, move state, callbacks) are game-library-private and never sent over the network

Only the `entityState_t` part is delta-encoded and sent to clients.

## Snapshots and Delta Compression

The server maintains a ring buffer of `clientSnapshot_t` records per client (in `server/sv_ents.c`). Each snapshot stores the full `entityState_t` set visible to that client at a given frame. `SV_WriteFrameToClient` compares the latest snapshot against the acknowledged one and emits only the fields that differ:

```c
// server/sv_ents.c
MSG_WriteDeltaEntity(&msg, &oldState, &newState, false, newState.number == cl->clientNum+1);
```

The client's acknowledged frame number is tracked per slot so the server can re-send if a snapshot was lost.

## Key Files

| File | Purpose |
|------|---------|
| `server/sv_main.c` | `SV_Frame`, `SV_Init`, `SV_ReadPackets` |
| `server/sv_ents.c` | `SV_BuildClientFrame`, `SV_WriteFrameToClient`, delta encoding |
| `games/warcraft-3/game/g_main.c` | `G_RunFrame`, `G_ClientBegin`, entity-per-frame dispatch |
| `games/warcraft-3/game/g_phys.c` | Entity movement, collision response |
| `games/warcraft-3/game/g_commands.c` | Player command handlers (move, attack, ability) |
| `games/warcraft-3/game/g_monster.c` | Unit lifecycle, animation state machine |
| `common/net.c` | Loopback + UDP transport shared with the client |
| `common/msg.c` | Message serialisation and delta encoding helpers |
