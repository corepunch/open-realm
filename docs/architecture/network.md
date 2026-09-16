# Network Architecture

OpenWarcraft3 uses the same runtime-dispatch networking model as Quake 2.  A
single send/receive API (`NET_SendPacket` / `NET_GetPacket`) handles all
communication; only the lowest layer changes path depending on whether the
destination is in the same process (loopback) or a remote machine (UDP).

## Address types (`netadr_t`)

Every packet is addressed with a `netadr_t`:

```c
typedef enum {
    NA_LOOPBACK,    // in-process ring buffer (zero copy, zero latency)
    NA_IP,          // unicast UDP
    NA_BROADCAST,   // broadcast UDP
} netadrtype_t;

typedef struct {
    netadrtype_t type;
    unsigned char ip[4];   // network byte order
    unsigned short port;   // network byte order
} netadr_t;
```

The `type` field is the only routing key.  `ip` and `port` are only
inspected when `type` is `NA_IP` or `NA_BROADCAST`.

## Loopback (listen-server / single-process)

When the executable starts with `+map` it runs both the server and the local
client in the same process.  The local client slot is assigned address type
`NA_LOOPBACK`; no socket is involved.

Two 256 KiB ring buffers carry traffic in each direction:

```
  bufs[NS_CLIENT]  ←  client writes (clc_move, clc_connect, …)
  bufs[NS_SERVER]  ←  server writes (svc_packetentities, svc_serverdata, …)
```

Sending to `NA_LOOPBACK` appends a 4-byte little-endian length prefix followed
by the payload into the appropriate ring buffer.  Receiving polls the other
side's ring buffer.

```c
// common/net.c
void NET_SendPacket(NETSOURCE netsrc, int length, const void *data, netadr_t to) {
    switch (to.type) {
    case NA_LOOPBACK:
        NET_SendLoopPacket(netsrc, length, data);
        break;
    case NA_IP:
    case NA_BROADCAST:
        NET_SendUDPPacket(length, data, to);
        break;
    }
}

int NET_GetPacket(NETSOURCE netsrc, netadr_t *from, LPSIZEBUF msg) {
    int r = NET_GetLoopPacket(netsrc, from, msg);  // loopback first
    if (r) return r;
    return NET_GetUDPPacket(netsrc, from, msg);    // then real socket
}
```

## UDP (remote client)

When the client searches LAN servers or starts a remote connection, it calls
`NET_Config(true)`.  This opens a client socket on an ephemeral port.  When a
LAN lobby is hosted, the server socket opens on `game_port`, defaulting to
`PORT_SERVER` (27910).

UDP datagrams are sent raw, matching Quake 2.  An out-of-band packet begins
with the normal `-1` message marker in the payload; there is no extra network
length prefix.

## Initialisation

`NET_Init()` is called once from `main.c` and only clears loopback state. UDP
sockets are opened and closed through `NET_Config(multiplayer)`, matching
Quake 2:

| Mode | Call | Effect |
|------|------|--------|
| Local map (`+map`) | `NET_Config(false)` or no multiplayer config | Loopback only; no UDP sockets |
| LAN lobby host | `NET_Config(true)` | Server socket on `game_port`, client socket on an ephemeral port |
| LAN search / remote connect | `NET_Config(true)` | Client socket on an ephemeral port, server socket available if hosting |

`NET_Shutdown()` calls `NET_Config(false)` to close any open sockets.

## Connection handshake

### Remote client → server

1. Client calls `CL_Connect(host, port)` which resolves the hostname via
   `NET_StringToAdr` and sends an out-of-band `"connect"` datagram to the
   server.
2. The server's `SV_ReadPackets` reads the datagram, checks that the payload
   starts with `"connect"`, and calls `SV_DirectConnect(from)` to allocate a
   new client slot with `NA_IP` type.
3. From this point the normal `clc_*` / `svc_*` message exchange proceeds over
   UDP, identical to the loopback exchange.

### Local client (loopback)

`SV_Map` calls `SV_ClientConnect()` which allocates slot 0 with type
`NA_LOOPBACK`.  No network handshake is required; the client sends a
`clc_connect` message through the ring buffer and the server responds with
`svc_serverdata`.

## Command-line interface

```sh
# Listen server + local client (loopback; no real socket traffic)
openwarcraft3 -data=/path/to/Warcraft3 +map Maps\Campaign\Human02.w3m

# Remote client, default port (PORT_SERVER = 27910)
openwarcraft3 -data=/path/to/Warcraft3 -connect=192.168.1.10

# Remote client, explicit port
openwarcraft3 -data=/path/to/Warcraft3 -connect=192.168.1.10:27910
```

| Argument | Description |
|----------|-------------|
| `-data=<folder>` | Warcraft III data folder containing MPQs and optional loose maps |
| `+map <path>` | Internal MPQ path of the map to load; starts a listen server |
| `-map=<path>` | Compatibility form for `+map <path>` |
| `-connect=<host[:port]>` | Hostname or IP of the server to join; starts a remote-client session |

`+map` and `-connect` are mutually exclusive.  Omitting both (with a valid
`-data`) starts the client menu.

## Entity snapshot flags

`entityState_t.flags` is a `USHORT` serialized as `NFT_SHORT` by `common/msg.c`. Engine-level bits describe generic client
presentation/interaction capabilities; game modules decide when to set them in server-authored snapshots. `EF_HOVER_HEALTH` means the
client may expose the entity's compressed health/mana through a server-declared world-hover layout. WC3 and WoW author this capability
per recipient; SC2 currently sends an empty hover layer and does not set it. `EF_HOSTILE` and `EF_NEUTRAL` are recipient-relative
presentation relationships; when neither is set, game-specific renderers may treat the entity as friendly. Any new field, flag, or
packed value in `entityState_t` requires
a `MSG_WriteDeltaEntity`/`MSG_ReadDeltaEntity` round-trip test in `tests/test_net.c`.

## Key files

| File | Purpose |
|------|---------|
| `common/net.c` | `NET_SendPacket`, `NET_GetPacket`, loopback buffers, UDP socket |
| `common/net.h` | `netadr_t`, `netchan_t`, `NETSOURCE`, public API |
| `common/main.c` | CLI parsing, `NET_Init`, mode selection |
| `server/sv_init.c` | `SV_ClientConnect` (loopback slot), `SV_DirectConnect` (UDP slot) |
| `server/sv_main.c` | `SV_ReadPackets` — OOB routing and per-slot dispatch |
| `client/cl_main.c` | `CL_Connect`, `CL_ReadPackets` |

## See Also

- [Server-Authored UI Payloads](ui-payloads.md) — `svc_layout` frame payload contract and unsigned size-byte handling

## LAN lobby and startup

Waiting connections receive pending messages or `svc_nop` at a one-second interval, including the listen
server's loopback client. `ss_lobby` runs transport without advancing the simulation. Keepalives continue
for `cs_connected` peers while other players are already running the map; cancelling still uses the normal
disconnect/shutdown path. The client's ordinary timeout remains enabled for a vanished host.

Startup follows Quake II's client-requested configstring and baseline pages. Remote startup messages are
bounded by `BZ_SIGNON_SIZE` (1400 bytes, leaving UDP/IP header room within a 1500-byte LAN MTU); loopback
retains the engine message budget. `svc_mirror` asks for the next `configstrings <index>` or `baselines <index>`
page. The final baseline reply is `precache`, which opens the client registration gate; registration then queues `begin`. Opening that
gate at the first baseline request would let registration and gameplay overtake the remaining entity pages.
The early loading presentation still puts its media before the eight binary loading-layout configstrings.

`SV_SetConfigString` does not queue live resynchronization while `ss_loading`: every connecting client fetches
those values through signon. Runtime changes in `ss_game` still mark the slot for broadcast. Broadcasting the
initial table as well as serving signon pages defeats the packet budget by filling a connected client's pending
message before its next request.

The September 10 reproduction used two macOS processes and `Maps/(2)PlunderIsle.w3m`. The idle host timed out
after 10,034 ms without server traffic. The remote configstring reply was 25,395 bytes and failed `sendto`
with `EMSGSIZE` (40); paging exposed 10,772 bytes of queued initial-table updates already included in that reply. A subsequent remote-only
world-registration crash resolved to `G_WorldReadFile` calling an uninitialized game import. A listen server
masks that boundary error because it initializes the game module before client registration.

An instrumented two-process audit of the same ROC map measured the complete configstring reply's table
at 14,612 bytes, including each entry's opcode, index and string terminator (excluding the early binary loading slots):

| Pool | Entries | Wire bytes |
|------|--------:|-----------:|
| Models | 54 | 2,066 |
| Sounds | 111 | 4,950 |
| Images | 129 | 7,301 |
| Font/size pairs | 7 | 173 |
| World/scope and other metadata | 7 | 122 |
| Total | 308 | 14,612 |

The original packet decomposes exactly as `10,772 + 14,612 + 11 = 25,395`: queued duplicate updates,
the table, and `svc_mirror` plus `"baselines"` and its terminator. These are indexed resource names and metadata,
not asset-file contents. After suppressing loading-time live updates, the audit observed zero pending bytes
at the first configstring request. The table volume is reasonable for the map's units, creeps, projectiles,
world models, sound sets and UI, but is not a minimal dependency list:

- Loading preparation registers 2 models, 59 images and 4 font styles (3,780 wire bytes). Its media is sent
  early and again in the complete table. Native `Loading.fdf` includes `StandardTemplates.fdf`; eagerly
  indexing these declarations accounts for 3,580 bytes of images, including unused button/scrollbar templates.
  A future reduction belongs in registration of consumed FDF resources, not filtering arbitrary network entries.
- Sixteen image entries (914 bytes) resolve to paths also used by other entries. They are distinct raw skin keys
  or a literal path versus a skin key, not repeated registrations of the same key. `SV_FindIndex` deduplicates
  raw keys; merging resolved paths would change their independently addressable configstring identity.
- `CS_WORLD` and `CS_ASSET_SCOPE` legitimately repeat the map path for different consumers; font sizes likewise
  require distinct entries. The figures above describe this map/edition, not a fixed protocol budget.

`CS_IMAGES` has 2,047 usable nonzero slots (`MAX_IMAGES == 2048`). The earlier 255-slot limit was too small for
Warcraft III's long-lived HUD: command cards, build menus, inventories, status panels, and FDF chrome can discover new art throughout
a map session, and `SV_FindIndex()` cannot safely recycle an occupied network slot while retained layouts still reference its number.
The larger table changes only index capacity; signon still serializes nonempty entries rather than the entire reserved range, and the
renderer-local texture registry remains independently unbounded.

Runtime resource publication is ordered with dependent UI payloads. `SV_FindIndex()` marks a new slot unsynced;
`SV_QueuePendingConfigStrings()` appends those `svc_configstring` records to every current client before `PF_Unicast()` appends and
transmits the layout/window payload that may contain the new index. The regular frame-send path calls the same queue helper. Do not
restore deferred-only publication: an immediate unicast can otherwise reach the client one packet before the configstring it uses.

To repeat the audit, temporarily log nonempty entries and `SV_ConfigStringWireSize` at the first
`SV_Configstrings_f` request, then run the bounded paired reproduction below. The loading layout now travels
separately through `svc_loading_screen`; see the [loading transport contract](../games/warcraft-3/loading-and-assets.md#loading-screen-ownership).
Inspect raw keys as well as `ge->GetThemeValue` output to distinguish aliases from duplicate registration;
remove the instrumentation and rebuild afterward.

Reference implementations: [Quake II server sending](https://github.com/id-Software/Quake-2/blob/master/server/sv_send.c)
(`SV_SendClientMessages`) and [startup commands](https://github.com/id-Software/Quake-2/blob/master/server/sv_user.c)
(`SV_Configstrings_f`, `SV_Baselines_f`). These lifecycle rules use this engine's existing messages; the raw
UDP transport is not Quake II's complete sequenced/reliable netchan implementation.

Regression coverage lives in `games/warcraft-3/tests/test_server_net.c` (idle loopback/UDP keepalives,
loading-time versus runtime updates, complete paged UDP configstrings/baselines) and `tests/test_net.c`
(the precache gate). `make test-server-net` requires permission to bind local UDP sockets.

WC3 builds `common/world_w3.c` into the executable with `BZ_CLIENT_WORLD`. The engine's `CM_LoadMapFormat`
therefore uses engine filesystem/allocation functions. The same WPM reader feeds either the client's terrain-cell
storage (placement previews) or the game's routing buffers; routing jobs and edict-dependent obstacle handling
remain in `common/routing.c`, compiled only into the game module. `CM_SetupPathMap` and
`CM_GetPathingFlagsAt` have client implementations for this reason. Do not fix remote registration by initializing
a local server, skipping WPM data, or guarding NULL game imports. The crash originally traversed
`CL_PrepRefresh -> CM_LoadMap -> G_WorldReadFile` with `gi.ReadFile == NULL`; compiling only the map-format reader
without its path-data consumer still reaches `G_WorldMemAlloc` through `CM_ReadPathMap` and crashes.

In-engine input tests must set client collision bounds explicitly: game test-world bounds are not the client world.
The `client_world` regression checks terrain-cell lookup, replacement, and teardown. `nm build/bin/openwarcraft3`
should show defined text symbols for `CM_LoadMapFormat`, `CM_SetupPathMap`, and `CM_GetPathingFlagsAt`.

Keep the implementations included by `games/warcraft-3/game/g_world.c` under hidden symbol visibility.
The executable and game deliberately own separate `world` records, map readers, and path buffers. ELF symbol
interposition otherwise redirects game calls to identically named executable functions. Linux CI diagnostics
confirmed `CM_SetupTestPathmap(64, 64, ...)` entered the client initializer and left game routing at `0x0` with
NULL storage, causing movement/footprint failures and a crash; macOS's normal symbol binding masked the error.
Hide the whole game world implementation, including its state, rather than only the two pathmap functions.
`wc3_pathfinding.terrain_flags_and_routing_share_game_storage` checks blocked/open replacement through both
flag and routing queries; the existing client-world tests independently exercise the engine copy. On Linux,
`readelf -Ws build/lib/libgame-wc3-test.so` should show game `CM_*` implementations and `world` as local symbols.

For a bounded two-terminal reproduction, write a host script and start the second terminal during its wait:

```sh
python3 - <<'PYLAN'
from pathlib import Path
Path('/tmp/lan-check.cfg').write_text(
    'wait\n' * 10 +
    'lobby_start "Maps/(2)PlunderIsle.w3m"\n'
    'lobby_config 2 2 PlunderIsle\n'
    'lobby_slot 0 1 0 1 1 0 0 Host\n'
    'lobby_slot 1 1 1 0 1 1 1 Open\n' +
    'wait\n' * 600 + 'map "Maps/(2)PlunderIsle.w3m"\n')
PYLAN
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +set vid_hidden 1 +com_maxfps 30 +com_frame_limit 1500 +exec /tmp/lan-check.cfg
# Second terminal, after the host prints "Lobby initialized":
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc -connect 127.0.0.1:27910 +set vid_hidden 1 +com_maxfps 30 +com_frame_limit 1500
```

Use an installed melee map path on both processes. `127.0.0.1` exercises UDP; `localhost` selects the in-process
loopback transport. Omit the final `map` command to verify an idle lobby. `+set vid_hidden 1` is required for a
hidden window; `+vid_hidden 1` alone can be parsed too late to hide it. Mac display access and local socket
permissions are required even for these hidden runs. Both peers must run the updated binary for `svc_nop` and
the final `precache` handshake.

Verification on September 10: a bounded idle lobby survived 800 frames at 30 fps (about 26 seconds); the
PlunderIsle UDP guest and host both sent `begin`, the server called `G_ClientBegin` for players 0 and 1 at distinct
start locations, and both processes exited normally. `make test` passed 2,927 tests / 61,017 assertions.

For hidden-window gameplay screenshots, set `cl_camera_edge_scroll` to `0`; otherwise the inactive window's
pointer can pan the camera into unexplored fog. For a command-line guest, also use
`+set cl_start_menu menu_ingame +screenshot 5`: `CL_Connect` defers the startup command tail until the first
active frame, so the default queued `menu_main` would otherwise reopen after loading. This CLI-only diagnostic
setting is unnecessary when joining through the LAN menu. Final engine screenshots confirmed the blue guest
and red host at their respective bases with terrain, units, fog, and HUD visible.
