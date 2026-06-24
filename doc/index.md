<img width="647" height="88" alt="OpenWarcraft3 logo" src="https://github.com/user-attachments/assets/67313005-70d7-4e68-97a3-3e0ff1e788ed" />

**OpenWarcraft3** is an open-source implementation of Warcraft III that uses SDL2 and runs on Linux and macOS.

It was developed using War3.mpq from Warcraft III v1.0 as reference, with ongoing support for version 1.29b.

<a href="https://youtu.be/vg7Jm046vcI">▶ Watch the demo on YouTube</a> · see screenshots below

## Download

Pre-built binaries for Linux and macOS are available on the [Releases page](https://github.com/corepunch/openwarcraft3/releases/latest).

You can also download the latest build artifact from the [CI workflow runs](https://github.com/corepunch/openwarcraft3/actions/workflows/c-cpp.yml) (click the most recent successful run and download `openwarcraft3-linux-x64`).

<p align="center">
  <img src="https://github.com/user-attachments/assets/c76a93af-1801-402e-83bc-b3e0a4462312" width="31%" style="margin-right:2%;" />
  <img src="https://github.com/corepunch/openwarcraft3/assets/83646194/643c7aa7-2b91-469c-857e-0f6910c939af" width="31%" style="margin-right:2%;" />
  <img src="https://github.com/corepunch/openwarcraft3/assets/83646194/a79e447d-e42c-4468-b4ca-3d212efe346a" width="31%" />
</p>

## Getting Started

### 1. Clone

```bash
git clone git@github.com:corepunch/openwarcraft3.git
cd openwarcraft3
```

### 2. Install Dependencies

The build requires **SDL2**. MPQ reading is handled by the in-tree `common/mpq.c` implementation, and JPEG texture decoding uses the in-tree `renderer/stb/stb_image.h`.

**macOS** (via [Homebrew](https://brew.sh/)):

```bash
brew install sdl2
```

**Linux** (Ubuntu/Debian):

```bash
sudo apt-get install libsdl2-dev
```

### 3. Build

```bash
make build
```

Compiles the engine and game libraries (`shared`, `jass`, `sheet`, `renderer`, `game`, `ui`) and the `openwarcraft3` executable into `build/`.

The default Warcraft III libraries are built from engine sources plus `games/warcraft-3/`. Warcraft III-specific script, sheet, game, renderer, UI, and test sources live under that tree. Alternate game builds use the same engine sources with `games/world-of-warcraft/` or `games/starcraft-2/`.

### 4. Run

```bash
make run
```

Runs `openwarcraft3` from `build/bin/` using the data folder configured in the Makefile.

Useful run targets:

- `make run` — start the client menu using `WC3DATA`
- `make run-map` — start a listen-server game using `MAP`
- `make run-ui-text` — render one UI frame through the stdout renderer

For deterministic UI diagnostics without a window:

```bash
make run-ui-text
```

This uses `r_module=stdout`, disables networking, runs the command from `ui_start_command`, prints renderer calls to stdout, and exits after one frame.

### (Optional) Download Warcraft III 1.29b assets

```bash
make download
```

Downloads a ~1.2 GB installer from `archive.org` into the `data/` folder. Skip this step if you already have a `War3.mpq` and update the `MPQ` variable in the Makefile to point to it.

---

## Architecture

### Client-Server Architecture

OpenWarcraft3 uses a strict client-server separation where all game logic runs exclusively on the server and clients are responsible only for rendering and input.

The **server** hosts the selected game library. For Warcraft III, the source lives in `games/warcraft-3/game/` and is built as `libgame` behind a Quake-style function table boundary. It maintains the authoritative game state: all entities, their positions, health, current animations, and AI state. The server processes player commands, runs the game simulation each frame, and sends the resulting state to clients.

The **client** (`client/`) captures user input via SDL2, forwards commands to the server, receives the updated game state, and renders it using the renderer API. Generic renderer sources live in `renderer/`; selected-game render policy and model/map formats live in `games/<game>/renderer/`. The client never runs game logic directly — it is purely a display and input layer.

Communication between the client and server happens through the network layer (`common/net.c`), which follows the Quake 2 runtime-dispatch model.  The routing decision is made at runtime on `netadr_t.type`:

- **Loopback** (`NA_LOOPBACK`) — when both server and client run in the same process (started with `+map`; `-map=` is still accepted), two 256 KiB ring buffers carry traffic in each direction with zero latency and no socket overhead.
- **UDP** (`NA_IP`) — when the executable is started with `-connect=<host>`, it binds a non-blocking UDP socket and communicates with a remote listen server over the network.

```
# Listen server (loopback, single process)
SDL2 Input  →  Client (cl_main.c)  →  net.c ring buffer  →  Server (sv_main.c)
                    ↑                                               |
                    └──────────── net.c ring buffer ←──────────────┘

# Remote client (UDP)
SDL2 Input  →  Client (cl_main.c)  →  UDP socket  →  Server (sv_main.c)
                    ↑                                      |
                    └─────────── UDP socket ←──────────────┘
```

See the [Network Architecture](architecture/network.md) page for the full design, wire format, and CLI reference. See [Runtime Modules and Cvars](architecture/runtime.md) for config files, module cvars, and stdout renderer diagnostics.
See [Ability Coverage](games/warcraft-3/docs/architecture/ability-coverage.md) for the current game-side ability registry and reference parity checklist, and [ROC Ability Checklist](games/warcraft-3/docs/architecture/roc-ability-checklist.md) for the Reign of Chaos ability list.

### Runtime Configuration

OpenWarcraft3 uses Quake-style cvars and config files. Defaults live in `share/default.cfg`; generated user settings are written to `share/config.cfg`; optional local overrides can be placed in `share/autoexec.cfg`.

Important runtime cvars:

| cvar | Default | Purpose |
|------|---------|---------|
| `r_module` | `renderer` | Renderer backend: `renderer` or `stdout` |
| `ui_module` | `ui` | UI module name |
| `g_module` | `game` | Game module name |
| `ui_start_command` | `menu_main` | First client-side UI command |
| `com_frame_limit` | `0` | Exit after N frames |

## Build System

The default Warcraft III build produces the engine/game libraries and one executable:

1. **libshared** (`shared/`) — mathematics (vectors, matrices, quaternions, geometric primitives); no external dependencies
2. **libjass** (`games/warcraft-3/jass/`) — Warcraft III JASS VM
3. **libsheet** (`games/warcraft-3/sheet/`) — Warcraft III SLK/profile parser
4. **librenderer** (`renderer/` + `games/warcraft-3/renderer/`) — generic renderer backend plus Warcraft III model/map hooks; depends on `libshared`, SDL2
5. **libgame** (`games/warcraft-3/game/`) — server-side Warcraft III game logic; depends on `libshared`
6. **libui** (`games/warcraft-3/ui/`) — client-side FDF parser, command-driven screen controller, and UI renderer
7. **openwarcraft3** — main executable linking the runtime libraries plus SDL2

Alternate builds follow the same shape: `openwow` links `libgame-wow`, `librenderer-wow`, and `libui-wow` from `games/world-of-warcraft/`; `opensc2` links `libgame-sc2` and `librenderer-sc2` from `games/starcraft-2/`.

The renderer module is intentionally compound. Engine renderer code in `renderer/` owns common GL, scene, font, texture, and diagnostic behavior. The selected game's `games/<game>/renderer/` sources implement the `R_Game*` hooks in `renderer/r_game.h`, so the engine renderer does not branch on MDX/M2/M3 model formats.

The build is driven by a `Makefile` for Linux/macOS. Run `make test` to execute the unit test suite.

## External Dependencies

- **MPQ layer** (`common/mpq.c`): in-tree Warcraft III MPQ reader; no StormLib dependency at build or run time. `mpqtool` CLI exposes `ls` and `cat` for archive inspection.
- **SDL2**: windowing, input, and OpenGL context
- **stb_image**: in-tree JPEG texture decoding
- **OpenGL**: 3D rendering (system-provided)

## Current Status

- Functional game implementation with basic Warcraft III features
- Support for original v1.0 assets with ongoing work for v1.29b compatibility
- Active development focused on expanding game feature completeness
- Cross-platform support for Linux and macOS environments

## Related Projects

### World of Warcraft Unity Implementations

Open-source World of Warcraft client and gameplay reimplementations in Unity, useful as design and architecture reference:

| Repository | Description | Stars |
|---|---|---|
| [Reinisch/Warcraft-Arena-Unity](https://github.com/Reinisch/Warcraft-Arena-Unity) | WoW client-server combat system in Unity with Photon Bolt | 581 |
| [CodeSteel/WoW-GameplayFeatures](https://github.com/CodeSteel/WoW-GameplayFeatures) | Core gameplay features in C# using Unity & FishNet | 11 |
| [LuigiElleBalotta/UnityStudyProject](https://github.com/LuigiElleBalotta/UnityStudyProject) | Study project using WoW models for rendering reference | 4 |
| [JensDevWoW/World-of-Warcraft-Unity](https://github.com/JensDevWoW/World-of-Warcraft-Unity) | WoW emulator built in Unity | 1 |
| [EloiStree/2024_08_26_WowMovingInUnity](https://github.com/EloiStree/2024_08_26_WowMovingInUnity) | WoW-style character movement system | 1 |
| [Sylian1337/World-of-Warcraft-Unity](https://github.com/Sylian1337/World-of-Warcraft-Unity) | WoW emulator built in Unity | 0 |

## License

This project is licensed under the [MIT License](https://github.com/corepunch/openwarcraft3/blob/main/LICENSE). Contributions are welcome!
