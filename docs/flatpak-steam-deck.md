# Flatpak And Steam Deck Packaging

## Contract

The Flatpak packages only OpenRealm-owned runtime files. It does not ship, copy,
or download Warcraft III retail assets.

The x86_64 package uses:

- app ID `io.github.corepunch.OpenRealm`;
- `org.freedesktop.Platform//25.08` and `org.freedesktop.Sdk//25.08`;
- `BUILD=release GL_BACKEND=gl MSAA=0`;
- `/app/bin/openwarcraft3` as the engine executable;
- `/app/lib/libshared.so`, `libjass.so`, `libsheet.so`, `librenderer.so`,
  `libgame.so`, and `libmenu.so` from the same build;
- the complete generated `build/share/` tree copied to `/app/share/`.

Do not reconstruct the generated share tree in packaging code. `make
openwarcraft3` already runs `install-share`, and Linux binaries/libraries use
`$ORIGIN` RUNPATHs that match `/app/bin` and `/app/lib`.

The Flatpak entry point is `/app/bin/openrealm-start`. Direct command-line
launches may still pass `-data`, `-tft`, `-roc`, cvars, and commands through the
wrapper.

Warcraft III's shipped video policy enables `vid_native 1` and
`vid_fullscreen 1`. SDL therefore uses fullscreen-desktop at the current display
resolution without a mode switch. In Steam Deck handheld mode this is normally
1280x800; when docked, OpenRealm follows the desktop resolution exposed by
Gamescope/the desktop session. `vid_native 0` returns control to the persisted
`vid_mode` fallback table.

## Writable State

Unix home-directory resolution follows the XDG base-directory contract:

```text
$XDG_DATA_HOME/<game>/
```

with `~/.local/share/<game>/` only as the fallback when `XDG_DATA_HOME` is
unset or not absolute. For Warcraft III this is where generated `config.cfg`, `autoexec.cfg`,
and other writable game state live.

Flatpak sets its XDG paths inside the application sandbox, so no broad
`--filesystem=home` permission is required for normal OpenRealm state.

The launcher separately stores the selected Warcraft III portal path in:

```text
$XDG_CONFIG_HOME/openrealm/warcraft3-data-path
```

Use the desktop launcher action **Select Warcraft III Data** or run
`flatpak run io.github.corepunch.OpenRealm --select-data` to replace that selection.

## Retail Data Selection

If no explicit `-data` argument is supplied, `openrealm-start` validates the
saved data path. A valid root contains a top-level `War3.mpq` (matched
case-insensitively).

When no valid path is saved, `/app/bin/openrealm-flatpak-helper prompt-data`
first explains why OpenRealm needs original Warcraft III data and asks whether
to continue. Choosing **Choose Warcraft III folder** then runs
`/app/bin/openrealm-flatpak-helper pick-data`, which calls
`org.freedesktop.portal.FileChooser.OpenFile` with `directory=true` over
GIO/GDBus. The chooser title is **Select Warcraft III game data folder** and
its accept action is **Use this folder**. Folder selection is part of FileChooser portal version 3 and newer.
The portal may add the selected location to the Documents portal; those grants
remain usable across sessions. The launcher still revalidates the saved path on
every start and opens the chooser again if that grant/path is no longer usable.
No general home-directory filesystem permission is required.

The helper supplies a unique `handle_token` and subscribes to the predictable
request object before calling `OpenFile`, which follows the portal request
contract and avoids losing an immediate `Response` signal. It also accepts an
older portal returning a different request handle.

Selection flow:

```text
saved path absent/invalid
        -> explain original-data requirement + Continue/Cancel
        -> XDG FileChooser portal
        -> select directory
        -> require top-level War3.mpq
        -> save returned portal-visible path
        -> detect top-level War3x.mpq
        -> -tft when present, otherwise -roc
        -> exec /app/bin/openwarcraft3 -data <path> ...
```

An explicit `-tft` or `-roc` wins over automatic expansion detection. An
explicit `-data` bypasses the launcher picker and leaves edition selection to
the supplied engine arguments.

Do not add a permanent Warcraft-data filesystem permission to the manifest as a
shortcut around the portal.

## Packaged Artwork

For the initial Flatpak/Steam Deck package, every packaged image is sourced from
`docs/images/openrealm.png`. The manifest uses it directly for Steam grid/hero
artwork, center-crops the same source for the desktop/AppStream icon, and the
AppStream screenshot metadata also points at that image. This is intentionally a
temporary single-artwork setup.

## Steam Deck Shortcut Integration

When OpenRealm is launched outside Steam and no matching Flatpak shortcut is
present, the launcher offers an explicit optional **Install Steam shortcut**
prompt. Choosing **Not now** leaves Steam untouched; choosing **Install Steam
shortcut** performs the integration. The prompt is
implemented by `openrealm-flatpak-helper` with SDL so it does not add a GUI
toolkit dependency.

`steam_shortcut.py` is the MIT-licensed helper supplied from the proven
Nox-Decomp packaging workflow. Keep its original copyright/license header. It:

- detects the active Steam userdata directory, checking the sandbox-visible
  `$XDG_DATA_HOME/Steam` mapping first and then native/Flatpak Steam fallbacks;
- safely parses and rewrites `shortcuts.vdf` with a backup;
- records `FlatpakAppID=io.github.corepunch.OpenRealm`;
- installs packaged grid/hero/icon artwork;
- configures Valve's `controller_neptune_gamepad+mouse.vdf` template when it is
  available.

The resulting non-Steam shortcut launches:

```text
/usr/bin/flatpak run io.github.corepunch.OpenRealm --skip-steam-install
```

`SteamAppId`, `SteamGameId`, and `--skip-steam-install` suppress the prompt so a
Steam launch cannot recursively offer to install itself.

Flatpak remaps the native-Steam `xdg-data/Steam/...` grants beneath the
sandbox's `$XDG_DATA_HOME/Steam/...`; `steam_shortcut.py` must therefore use
`$XDG_DATA_HOME` rather than assuming `$HOME/.local/share` is the host XDG data
directory. Debian/Ubuntu's native Steam package commonly installs beneath
`~/.steam/debian-installation` and exposes `~/.steam/steam` as a symlink to it,
so the manifest also grants `~/.steam:rw`. The explicit
`~/.var/app/com.valvesoftware.Steam/...` candidates cover a host Flatpak Steam
installation. If none exposes a `userdata` directory, the launcher logs that
Steam integration was skipped instead of silently suppressing the install
prompt.

Only Steam-owned paths required by this opt-in feature are writable/readable in
`finish-args`:

```text
~/.steam
$XDG_DATA_HOME/Steam/userdata
~/.var/app/com.valvesoftware.Steam/data/Steam/userdata
$XDG_DATA_HOME/Steam/controller_base
~/.var/app/com.valvesoftware.Steam/data/Steam/controller_base
$XDG_DATA_HOME/Steam/steamapps/common/Steam Controller Configs
~/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Steam Controller Configs
```

The `~/.steam` grant is intentionally limited to Steam's own directory; do not
widen it to the whole home directory.

The current profile is intentionally a gamepad-and-mouse Steam Input template,
not native OpenRealm controller support.

## Build And Bundle

The packaging files live in `dist-scripts/flathub/`.

On a Linux development machine with `flatpak`, `flatpak-builder`, and `ostree`
installed:

```bash
cd dist-scripts/flathub
./deps.sh
./build.sh
```

`deps.sh` installs the Freedesktop 25.08 runtime/SDK into the user's Flatpak
installation. Freedesktop SDK 25.08 provides `sdl2-compat` as its SDL2 source/ABI
compatibility layer; the Flatpak helper probes the current `sdl2-compat` pkg-config
name first and falls back to `sdl2`. OpenRealm itself deliberately keeps its
existing SDL2 includes and `-lSDL2` link contract. Do not bundle a private SDL2
unless this compatibility path is shown to be insufficient on the target runtime.

`build.sh` keeps Flatpak Builder state and the temporary OSTree
repository under `${TMPDIR:-/tmp}/openrealm-flatpak`, outside the source tree,
and writes the finished bundle into the normal repository build directory:

```text
build/io.github.corepunch.OpenRealm.flatpak
```

Install a local bundle with:

```bash
cd dist-scripts/flathub
./install.sh
```

Uninstall the per-user Flatpak with:

```bash
cd dist-scripts/flathub
./uninstall.sh
```

The uninstaller removes an OpenRealm Steam shortcut/artwork created by the
Flatpak where possible, then removes the Flatpak. It preserves OpenRealm's
Flatpak-owned configuration/saves by default. For a complete purge use:

```bash
./uninstall.sh --delete-data
```

Use `--keep-steam-shortcut` only when intentionally leaving the non-Steam
shortcut behind. Restart Steam after shortcut removal so its library view is
refreshed.

The manifest uses a local `type: dir` source so developer and release builds
package the exact checked-out tree. A future Flathub submission should replace
that source with an immutable upstream tag/commit and checksum policy rather
than treating this development manifest as a final Flathub submission.

## CI And Release Workflows

`.github/workflows/c-cpp.yml` builds the Flatpak as the final CI job. The
`build-flatpak` job waits for the unit-test, Linux native-build, and Windows
native-build jobs to succeed before it runs. Frequent CI builds run inside the
official Flathub Freedesktop 25.08 build container and invoke the pinned
`flatpak-github-actions` builder action instead of installing Flatpak tooling and
the runtime/SDK with `apt`/`deps.sh` on every fresh GitHub-hosted runner. Builder
state under `.flatpak-builder` is restored/saved through the action cache, while
Flatpak Builder still invalidates changed local-source modules from the checked-
out tree. The job uploads `build/io.github.corepunch.OpenRealm.flatpak` as the
`openrealm-flatpak-linux-x64` workflow artifact. This makes every successful
main/tag/PR CI run exercise the distributable Flatpak after the normal build
work has completed.

`dist-scripts/flathub/deps.sh` remains the supported local-developer setup path.
The separate release workflow intentionally keeps the local-style build flow so
its bundle/installer-ZIP assembly remains independent of the frequent CI job.

`.github/workflows/release.yml` keeps its separate Linux `flatpak` job for
published releases. It installs host Flatpak tooling, installs the 25.08
runtime/SDK for the CI user, builds the bundle, and uploads both
`build/io.github.corepunch.OpenRealm.flatpak` and a
`build/openrealm-flatpak-<tag>.zip` containing the bundle plus `install.sh` and
`uninstall.sh` to the same existing GitHub release as the native archives.

Keep Flatpak packaging independent from native release archives: both CI and
release bundles must build from source through the manifest so their runtime and
permissions stay reproducible.

## Verification

Do not treat a successful `flatpak-builder` run alone as Steam Deck validation.
After building, verify on a Deck or equivalent Flatpak desktop:

1. first launch opens the directory portal;
2. selecting a directory without `War3.mpq` is rejected;
3. a valid selected directory launches OpenRealm;
4. `War3x.mpq` selects TFT automatically and RoC-only data selects RoC;
5. the same portal path works after restarting the app;
6. generated config/state survives app restarts and updates;
7. `ldd /app/bin/openwarcraft3` from `flatpak run --command=sh` resolves the
   six OpenRealm libraries from `/app/lib`;
8. rendering, audio, networking, mouse/keyboard input, and the selected game
   data work under the sandbox;
9. opting into Steam integration creates one shortcut and does not prompt again
   when launched through Steam;
10. Steam sees the packaged artwork and the Deck gamepad+mouse template after a
    Steam restart.

This initial package is x86_64 only. Native OpenRealm controller bindings,
bespoke Steam Input mappings, ARM Flatpak builds, retail installer extraction,
and a formal Flathub submission are separate follow-up work.
