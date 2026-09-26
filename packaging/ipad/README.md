# OpenWarcraft3 on iPad

An iPad app (one executable plus six embedded frameworks) built directly with
Xcode SDK commands, following the `mapview/ui` Makefile workflow. No Xcode project or third-party
iOS dependencies are required beyond the SDL2 release tarball (downloaded and
built automatically). The default deployment target is iPadOS 16.

```sh
make ipad              # device bundle (arm64)
make ipad-simulator    # simulator bundle
make ipad-run          # build, install, launch in an iPad simulator
make list-devices
make ipad-deploy       # auto-select the only connected iPad, install + launch
make ipad-deploy DEVICE="iPad name or identifier"  # explicit device
make ipad-mac          # signed iPad build launched on an Apple silicon Mac
```

If `DEVICE` is omitted for deployment, the Makefile auto-selects the sole
available physical iPad, ignoring simulators, iPhones, and unavailable
devices. It fails if there are zero or multiple eligible iPads. Device
installation uses an installed development certificate and a matching
provisioning profile; optionally supply `TEAM=...`, `PROFILE=...`, and
`BUNDLE_ID=...`. The default bundle identifier is
`com.openwarcraft3.warcraft3`.

Outputs are `build/ipad/<SDK>-<ARCH>/warcraft3/Warcraft3.app`. Advanced builds
can invoke `make -f packaging/ipad/build.mk SDK=iphonesimulator IOS_MIN=16.0 app`
and override `BUILD_DIR` or `ARCH`.

## How it works

- `packaging/ipad/build.mk` compiles every engine module with
  `xcrun --sdk <iphoneos|iphonesimulator> clang` and links one app plus six
  embedded frameworks (`shared`, `jass`, `sheet`, `renderer`, `game`, `menu`).
  The framework split mirrors the desktop shared-library topology
  (`games/warcraft-3/game.mk`) deliberately: game and menu each carry a
  private copy of the stb_fdf parser (own globals, own host/theme bindings,
  and `g_world.c` even textually includes `world_w3.c`), so statically
  linking them into one image fails with ~150 duplicate symbols. iOS keeps
  the desktop isolation with `Frameworks/*.framework` dylibs instead of
  `.so` files; no source changes are needed for this.
- The renderer builds with `-DBZ_GL_ES3`: `renderer/r_local.h` already selects
  `<OpenGLES/ES3/gl.h>` under `TARGET_OS_IPHONE`, and `r_main.c` requests an
  ES 3.0 SDL GL context. Epoxy is never used on Apple targets. The macOS-only
  `NSApplication` activation-policy block in `r_main.c` is guarded out on iOS,
  where SDL owns the `UIApplication` lifecycle. WC3 links no Lua (the vendored
  `liblua.a` is a WoW-menu dependency) and no FFmpeg, matching the default
  desktop WC3 configuration.
- The SDL2 runtime (2.32.10, matching the macOS native build) is downloaded
  from the official release and built with CMake's Unix Makefiles generator
  for the selected SDK (`CMAKE_SYSTEM_NAME=iOS`, SDK sysroot, static only).
  No `.xcodeproj` is involved at any step.
- `tools/ipad/bundle.py` assembles the `.app`: project `share/` plus
  `games/warcraft-3/share/` under `Warcraft3.app/share/`, a universal
  1024x1024 app icon compiled by `actool`, and an `Info.plist` with iPad-only device family,
  `opengles-3` capability, file-sharing/Documents support, and a launch
  screen. Unlike the Orion port, no `UIApplicationSceneManifest` is set: SDL2
  provides its own app delegate and a foreign scene delegate would break
  startup. `tools/ipad/sign.py`, `run_simulator.py`, and `wrap_mac.py` behave
  like their `mapview/ui` counterparts.
- `common/main.c` resolves the iOS home directory to the Files-visible
  `Documents` folder and defaults `-data` there, so retail MPQs copied via
  Files, AirDrop, or USB (`War3.mpq`, `War3x.mpq`, …) are picked up with no
  command line. The read-only `share/` tree resolves beside the executable
  inside the bundle via the existing `SDL_GetBasePath()` probe.

## Game data

Retail Warcraft III data is not bundled (it is gigabytes and license-owned).
Copy the MPQs into the app's Documents folder — *On My iPad > OpenWarcraft3*
in Files once installed — using Finder file sharing, AirDrop, or
`xcrun devicectl` (one file per call):

```sh
xcrun devicectl device copy to --device <UDID> \
  --domain-type appDataContainer --domain-identifier com.openwarcraft3.warcraft3 \
  --source "/path/to/Warcraft III/War3.mpq" --destination Documents/War3.mpq
```

Without `war3.mpq` in Documents the app shows a "Warcraft III data not found"
alert and exits instead of starting with an empty (black) screen.

## iOS graphics gotchas (why the renderer changed)

Two small engine changes were required; both are no-ops on desktop:

- **Drawable framebuffer.** SDL on iOS renders into its own FBO
  (`SDL_uikitopenglview` `viewFramebuffer`, non-zero id) and
  `presentRenderbuffer:` presents whichever renderbuffer is bound. The
  engine's four "back to the drawable" sites bound id 0, so on iOS every
  frame drew nowhere and the swap presented a black buffer — while
  `r_stats` happily reported ~80 draws at 64 fps. `R_BindDefaultFramebuffer()`
  (`renderer/r_main.c`, declared in `renderer/r_local.h`) binds the FBO
  captured via `GL_FRAMEBUFFER_BINDING` right after `SDL_GL_MakeCurrent`
  (creation leaves SDL's FBO bound; `updateFrame` reallocates storage on the
  same ids across resizes, so the cached id stays valid). Desktop queries
  back 0, preserving exact behavior.
- **Depth buffer.** The window system provides an implicit depth buffer on
  desktop GL, but EGL-style drivers build the drawable from the SDL
  attributes. `R_InitRenderer` now requests `SDL_GL_DEPTH_SIZE 24` on iOS;
  the renderer clears and depth-tests unconditionally.
- `UIApplicationSupportsIndirectInputEvents` is set so trackpads and mice
  work through SDL.

Verified on an iPad simulator (software GLES3, 2064x2752 drawable): the app
mounts `War3.mpq`/`War3Local.mpq` from Documents, compiles all shaders, and
renders the Reign of Chaos main menu (logo, 3D battlefield backdrop, full
button list) identically to the desktop build.

## Touch status
SDL translates single-finger touch to mouse events, so menus and unit orders
respond to taps out of the box. Multi-touch gestures, pinch zoom, and the
software-keyboard flow have not been tuned for this build; see the in-game
input code (`client/cl_input.c`) for the desktop assumptions that still apply.

## Icons

`icons/warcraft3.png` is the Warcraft III orc (Grom Hellscream) cover art,
upscaled to 1024x1024. `tools/ipad/bundle.py` emits it as a single-size universal
1024x1024 app icon (converted with `sips`) and `actool` derives every iPad
size. Replace the file to rebrand.
