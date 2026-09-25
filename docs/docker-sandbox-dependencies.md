# Docker Sandbox Dependencies

This document records the Debian/Ubuntu packages installed in the development
[sandbox (docker sbx)](https://docs.docker.com/ai/sandboxes/) used to build and inspect OpenRealm. The recorded package names are the
explicit dependencies requested from APT; APT also installed their normal
runtime and development transitive dependencies.

## Environment

- Distribution: Ubuntu 26.04 LTS (`resolute`)
- Architecture: `amd64`
- Installation method: `apt-get` with `sudo`
- Sandbox reference: [Docker AI Sandboxes](https://docs.docker.com/ai/sandboxes/)

## Recreate the sandbox environment

The repository includes a Docker Sandbox environment file at `sbxenv.yaml` and a
local mixin kit at `tools/docker-sandbox/openrealm-dev/spec.yaml`. Docker Sandbox
environment files cannot declare APT packages directly, so the environment file
loads the local kit and the kit installs the package set recorded below when the
sandbox is created.

From the repository root, review the environment plan and then create or attach to
the sandbox:

```sh
sbx env plan
sbx env run
```

The default environment installs the normal OpenRealm build/debug dependencies,
radare2, and the Xvfb packages used for headless campaign runs. To include the
optional FFmpeg development libraries as well:

```sh
sbx env run --env-arg ffmpeg=1
```

Changing the kit selection or its arguments affects sandbox creation. Recreate an
existing environment with `sbx env rm` followed by `sbx env run` when those values
change. The recorded Ubuntu 26.04/amd64 versions below describe the environment in
which these dependencies were verified; `sbxenv.yaml` does not independently pin the
base Ubuntu release, APT package versions, or CPU architecture.

## OpenRealm build and debug tools

```sh
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  build-essential \
  gdb \
  libsdl2-dev \
  libgl-dev \
  libegl1-mesa-dev \
  zlib1g-dev \
  pkg-config \
  ca-certificates \
  git
```

These provide GCC/binutils and `make`, GDB, SDL2 headers and libraries, Linux
desktop OpenGL/EGL development files, zlib headers and libraries, and the
basic package/source-control utilities used by the project.

The verified toolchain versions in this sandbox were:

| Tool | Version |
| --- | --- |
| GCC | 15.2.0 |
| GNU Make | 4.4.1 |
| GDB | 17.1 |
| SDL2 (`pkg-config`) | 2.32.10 |

## Radare2 inspection tools

```sh
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y radare2
```

The Ubuntu package supplies both commands used by the Warcraft III binary
inspection workflow:

```sh
r2 -v
rabin2 -v
```

The installed version was radare2 6.0.7. APT additionally installed the
radare2 libraries and supporting packages such as Capstone, libmagic, libzip,
liblz4, and xxHash development files.

## Project build verification

After installation, the main Warcraft III target built successfully:

```sh
make -j2 openwarcraft3
```

## Headless Warcraft III campaign runs

The SDL client still needs an X11 display even when no physical desktop is
available. The sandbox uses Xvfb with software OpenGL:

```sh
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y xvfb xauth
```

Run the client through `xvfb-run`, hide the window, and bound execution with
`+com_frame_limit`:

```sh
xvfb-run -a env SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
  build/bin/openwarcraft3 -data 'data/warcraft-3' -roc \
  +set vid_hidden 1 +set sv_cheats 1 \
  +map 'Maps/Campaign/Prologue01.w3m' \
  +com_frame_limit 1800
```

Commands that must run after the campaign intro are placed in a temporary
config and executed after the map command. The config contains `camera edge 0`,
approximately 900 `wait` commands for the intro, then the diagnostic actions:

```text
camera edge 0
hero select
smartpoint -5056 -1344
camera selected
```

`camera edge 0` prevents local mouse-edge scrolling from moving the viewport;
`camera selected` places the view on the selected Hero after the move. This
sequence was used to verify the Prologue01 Circle of Power transition. The
bounded log showed the Circle receiving team 15, reaching the renderer as
`team=15`, and loading the team-15 texture. `hero select` requires
`sv_cheats 1` and selects the first live controllable Hero for headless/GDB
diagnostics.

## GDB command injection

Attach GDB to the running client after the campaign intro and call the game
command dispatcher directly. Find the process from another shell:

```sh
pid=$(pgrep -n -x openwarcraft3)
```

The dispatcher expects an argument vector whose first element is the command
name. Allocate that vector in GDB rather than using a compound literal:

```sh
gdb -q -batch -p "$pid" \
  -ex 'set pagination off' \
  -ex 'set $ent = G_GetPlayerEntityByNumber(0)' \
  -ex 'set $a = (char**) malloc(16)' \
  -ex 'set *((char**)$a) = (char*)"select"' \
  -ex 'set *((char**)$a + 1) = (char*)"309"' \
  -ex 'call G_ClientCommand($ent, 2, $a)' \
  -ex 'call free($a)' \
  -ex 'set $b = (char**) malloc(24)' \
  -ex 'set *((char**)$b) = (char*)"smartpoint"' \
  -ex 'set *((char**)$b + 1) = (char*)"-5056"' \
  -ex 'set *((char**)$b + 2) = (char*)"-1344"' \
  -ex 'call G_ClientCommand($ent, 3, $b)' \
  -ex 'call free($b)' \
  -ex 'set $c = (char**) malloc(16)' \
  -ex 'set *((char**)$c) = (char*)"camera"' \
  -ex 'set *((char**)$c + 1) = (char*)"selected"' \
  -ex 'call G_ClientCommand($ent, 2, $c)' \
  -ex 'call free($c)' \
  -ex 'detach' -ex 'quit'
```

Here `309` is the Prologue01 Hero and `(-5056,-1344)` is the nearest Circle
of Power in the test map. The current command implementation uses `select 309`;
`hero select` is the supported higher-level command for selecting the first
live controllable Hero. GDB pauses
the client while attached, so detach promptly after each call and allow the
game several seconds to process movement and trigger events.

## Optional FFmpeg support

The default build does not link FFmpeg. To enable Warcraft III pre-rendered
movie playback and background music decoding, install the five development
packages named by `games/warcraft-3/game.mk`:

```sh
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  libswresample-dev
```

`pkg-config` is already included in the base dependency set and is used to
resolve these libraries. Build the optional configuration with:

```sh
make FFMPEG=1 openwarcraft3
```

The `ffmpeg` command-line package is not required for compilation; install it
only when command-line media conversion tools are also needed.

The repository's `tools/r2_ability.sh` script uses base `r2` for string,
FourCC, and disassembly analysis. Its optional `pdg` path requires the
`r2ghidra` plugin; Ubuntu 26.04 does not provide an `r2ghidra` APT package,
so that plugin is not part of this sandbox setup.

## Not included

- `r2ghidra`: optional, not available as an Ubuntu 26.04 package.
- FFmpeg development packages: only needed for the optional `FFMPEG=1`
  Warcraft III movie/audio build path, not the default build; see above.
