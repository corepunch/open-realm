# Android bring-up

## Scope

The first target is Warcraft III on an arm64 Android tablet using SDL2 and GLES3.
`make android-probe` builds a standalone production-shader diagnostic.
`make android-warcraft3` builds the full Warcraft III runtime and a platform MPQ importer.
Retail game data is supplied by the user and is never included in either APK.
See [iPad packaging](https://github.com/corepunch/open-realm/blob/main/packaging/ipad/README.md) for the existing mobile module/source split and
[renderer platforms](build-and-renderer-platforms.md) for the GLES3 contract.

Keep SDK downloads, AVD images, logs, and screenshots under ignored `build/android/`.
Run commands from the repository root. Set `JAVA_HOME` to an installed JDK supported by the SDK tools
(replace `/path/to/jdk` below with its home directory):

```sh
export JAVA_HOME=/path/to/jdk
export ANDROID_HOME="$PWD/build/android/sdk"
export ANDROID_USER_HOME="$PWD/build/android/user"
export ANDROID_AVD_HOME="$PWD/build/android/avd"
```

## SDK and virtual device

Download the Apple Silicon command-line tools from the [official Android SDK page](https://developer.android.com/studio),
verify its published SHA-256, and extract its `cmdline-tools/` directory as
`build/android/sdk/cmdline-tools/latest/`. The initial setup used build 15859902 (Mac ARM64), SHA-256
`835b62a26162b229b441d1f6d4680383815a270809eb33522c0d480fa5002c4e`.

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$ANDROID_HOME" \
  'emulator' 'platform-tools' 'system-images;android-35;default;arm64-v8a'
mkdir -p "$ANDROID_AVD_HOME"
printf 'no\n' | "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" create avd \
  --name openrealm_tablet_api35 --package 'system-images;android-35;default;arm64-v8a' \
  --device medium_tablet --path "$ANDROID_AVD_HOME/openrealm_tablet_api35.avd"
```

Review and accept the SDK license when prompted. SDK tools build 15859902 still provides `sdkmanager`
and `avdmanager`; `sdkmanager` prints a deprecation notice recommending the newer Android CLI.
The selected image is Android 15 / API 35, arm64-v8a, without Google Play or Google APIs.

Before first boot, edit matching keys in `$ANDROID_AVD_HOME/openrealm_tablet_api35.avd/config.ini`
to use the smaller landscape tablet configuration verified here (the stock medium-tablet profile is 2560x1600):

```ini
avd.id=openrealm_tablet_api35
avd.name=OpenRealm Tablet API 35
hw.lcd.width=1280
hw.lcd.height=800
hw.lcd.density=160
hw.initialOrientation=landscape
hw.gpu.enabled=yes
hw.ramSize=2048
hw.keyboard=yes
hw.audioInput=no
hw.sdCard=no
disk.dataPartition.size=4G
```

Remove `sdcard.size` from that configuration. The Android emulator documentation notes that SD card
images are unsupported on Apple Silicon; app data remains on the virtual data partition.

Launch in a terminal (closing the emulator window stops the device):

```sh
"$ANDROID_HOME/emulator/emulator" -avd openrealm_tablet_api35 -port 5554 \
  -gpu auto -memory 2048 -cores 4 -no-snapshot -no-boot-anim \
  -camera-back none -camera-front none
```

Check completion of boot and collect graphics diagnostics:

```sh
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell getprop sys.boot_completed
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell getprop ro.build.version.release
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell getprop ro.product.cpu.abi
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell dumpsys SurfaceFlinger
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 exec-out screencap -p > build/android/emulator.png
```

`sys.boot_completed` must return `1`; an adb transport in `device` state alone does not prove boot completion.
Select the serial explicitly to avoid operating on a connected physical device.
Emulator GPU strings describe its translation stack, not physical Adreno/Mali compatibility or performance.
See [Android emulator commands](https://developer.android.com/studio/run/emulator-commandline).

## Verified local environment

The initial Apple Silicon setup booted successfully with emulator 37.2.12 and platform-tools 37.0.1.
`emulator -accel-check` reported Hypervisor.Framework available. Adb serial `emulator-5554` returned
`sys.boot_completed=1`, Android release `15`, ABI `arm64-v8a`, display `1280x800`, and page size `4096`.
SurfaceFlinger reported GLES 3.1 through ANGLE/Vulkan SwiftShader with `-gpu auto`:
CPU virtualization is accelerated, but graphics in this run uses software rendering.
The launcher appeared after boot.

`avdmanager` printed missing `devices.xml` diagnostics for this default image but returned success,
created the profile, and the emulator subsequently booted. Confirm the generated config and boot status
rather than treating the diagnostic alone as either fatal or proof of success.

## SDL2 / production shader APK

The opt-in `packaging/android/` target uses CMake with the NDK toolchain and the Android SDK's
`javac`/D8/AAPT2/zipalign/apksigner pipeline, without Gradle or Android Studio. It compiles
`renderer/r_shader.c` directly: shader bodies, declarations and the `vendor/gl_shader` compiler are
the production implementations, not copied GLSL. Hidden symbols and section garbage collection
discard unrelated renderer runtime functions; there are no fake engine imports or modified game APIs.

Install the additional build prerequisites:

```sh
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$ANDROID_HOME" \
  'platforms;android-35' 'build-tools;35.0.1' 'ndk;28.2.13676358'
mkdir -p build/android/downloads
curl -fL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz \
  -o build/android/downloads/SDL2-2.32.10.tar.gz
shasum -a 256 build/android/downloads/SDL2-2.32.10.tar.gz
tar -xzf build/android/downloads/SDL2-2.32.10.tar.gz -C build/android
make android-probe
make android-probe-run ANDROID_SERIAL=emulator-5554
```

The SDL source archive used here hashes to
`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
The Java compilation prepends the SDK's
`core-lambda-stubs.jar` to `android.jar`: Android's stub `LambdaMetafactory` alone cannot compile
SDL's Java lambdas. The builder uses Python ZIP support without requiring the JDK's `jar` executable.
SDK Build Tools 35 apksigner emits a Java 25 native-access warning; signing and verification succeed.

The APK supports arm64-v8a, minSdk 26 and targetSdk 35. It contains `libSDL2.so`, `libmain.so` and
the Java SDLActivity shim/subclass. No storage permissions, retail game files, or network permissions are needed.
The debug keystore, SDK, and outputs stay under ignored `build/android/`.

`android-probe-run` installs only to the explicitly selected serial, restarts `org.openrealm.probe`,
waits up to 45 seconds for that process's `RESULT PASS`, and saves `probe-logcat.txt` and `probe.png`.
Build-only output is `build/android/probe/openrealm-probe.apk`; use
`python3 tools/android/build_probe.py --help` for SDK, SDL source and output overrides.
The diagnostic stays open with an animated triangle; Android Back exits it.

### Runtime checks and observed result

- Compile/link all seven non-model descriptors, including terrain/default.
- Compile/link eight combinations of the model shader: instancing, shadows and MSAA defines,
  always with fog of war enabled. This tests shader variants, not actual multisample rendering.
- Keep the production 128-matrix bone palette and verify its final entry has a live uniform location.
- Upload the full palette through `gs_apply`; draw a synthetic triangle weighted entirely to bone 127.
- Translate that bone between two known positions and read both framebuffer locations. Expected
  foreground RGB is `(51,184,235)` and background is `(9,14,23)`; the two locations must exchange colors.
- Verify a 24-bit depth buffer and no GL error during pixel checks.

On the Android 15 ARM64 emulator: **15/15 shader programs passed**, both pixel checks passed,
and the final result was **PASS**. GLES reported 4096 vertex uniform vectors / 16384 components,
so this does not establish support on implementations exposing only the GLES3 minimum.
The production renderer and game code were unchanged. The fixture is synthetic, not an MDX asset loader test.
The UI shows the result over the SDL GL surface, outside immersive mode to avoid the Android first-run
fullscreen tutorial obscuring diagnostics.

APK v2/v3 signature verification and ZIP alignment checks passed. `llvm-readelf -lW` showed every
LOAD segment in both native libraries aligned to `0x4000` (16 KB). Runtime testing used a 4 KB-page image;
a 16 KB-page Android device/image and physical Adreno/Mali checks remain pending.

Official references: [SDL2 Android](https://wiki.libsdl.org/SDL2/README-android),
[NDK CMake](https://developer.android.com/ndk/guides/cmake),
[APK signing](https://developer.android.com/tools/apksigner).

## Full Warcraft III APK and archive import

```sh
make android-warcraft3
make android-warcraft3-run ANDROID_SERIAL=emulator-5554
```

These use the same SDK/JAVA_HOME/SDL prerequisites as the probe. Output:
`build/android/game/openrealm-warcraft3.apk`, package `org.openrealm.wc3`.
The native build tree is shared at `build/android/probe/native` to reuse SDL across APKs.
The builder disables incremental adb installation after an observed stalled transport;
`adb -s emulator-5554 reconnect` recovered that local transport without restarting other devices.

The launcher uses Android's multi-file `ACTION_OPEN_DOCUMENT` picker. It accepts the classic base,
language, expansion and patch archive names listed by `ArchiveStore.archiveName`, normalizing their case.
Reign of Chaos requires `war3.mpq`; the TFT button additionally requires `war3x.mpq`.
File presence enables the buttons; successful play still requires the corresponding complete retail resources.
The importer does not convert Reforged CASC data or unpack ZIP installers.

Selected content URIs are read on a worker thread into a temporary file in app-owned `files/data/`.
The importer checks available space when the size is known, copies in bounded chunks, syncs/closes the
output and provider stream, checks the actual size, and validates the staged archive with the production
`SFileOpenArchive` reader through `libmpqcheck.so`. Only then does an atomic rename replace the target.
Bad filenames, truncated/invalid archives, or stream read/close failures preserve the installed archive.
Transactions are per file: a later error in a multi-file batch does not undo earlier successful files.
Native validation establishes a readable archive, not the presence of every resource needed by Warcraft III.
No broad Android storage permission or persistent URI access is requested; game data is copied locally.

The Java launcher installs project-owned APK `assets/share/` into `files/share/` before play.
`GameActivity` runs in a separate `:game` process and passes ordinary `-data`/`-tft` arguments to SDL_main.
The native `common/main.c` Android paths are `files/share` for read-only defaults and `files/user`
for config/saves. Native stdout/stderr are retained in `files/engine.log`:

```sh
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 exec-out run-as org.openrealm.wc3 \
  cat files/engine.log > build/android/game/engine.log
```

CMake mirrors the desktop/iPad unity source partitions in separate `realm_shared`, `realm_sheet`,
`realm_jass`, `realm_renderer`, `realm_game`, `realm_menu` and `warcraft3` libraries.
ELF `-Bsymbolic` preserves module-owned FDF and world state; undefined symbols remain linker errors.
Assertions remain enabled, matching desktop release: CMake's default NDEBUG would remove the JASS loop bound.
`shared/test.c` uses Android's API-26-compatible `clock_gettime` instead of API-29 `timespec_get`.
The Spirit Link local `far` variable was renamed because Android zlib defines `far` as an empty legacy macro.
The renderer explicitly requests depth24 on Android, as on iPad.

### On-device regression checks without retail data

Generate `build/tests/tests.mpq` with the existing `make test-assets` workflow, then:

```sh
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 push build/tests/tests.mpq /data/local/tmp/openrealm-fixture.mpq
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell run-as org.openrealm.wc3 mkdir -p files
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell run-as org.openrealm.wc3 \
  cp /data/local/tmp/openrealm-fixture.mpq files/fixture.mpq
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell am instrument -w org.openrealm.wc3/.ImportTests
```

`ImportTests` checks successful imports, case normalization, unknown size, malformed MPQ, truncation,
path rejection, read failure and provider-close failure, preserving old bytes and cleaning staging files.
It loads every runtime library and installs the fixture into `cache/engine-smoke`, separately from user data.
It then starts the real SDL_main in the separate game process with `+dedicated 1 +test wc3_smoke.*`.
Only this explicit test launch loads `librealm_smoke.so`, containing the existing `t_smoke.c` tests;
normal play contains no registered smoke tests. Native startup mounts the archive, initializes the server
and game through GetGameAPI, and executes 6 assertions in 4 tests.
The fixture deliberately lacks retail resources: missing SLK diagnostics are expected here and do not
establish that the main menu or campaign works. The instrumentation never installs fixture data into `files/data`.

An initial successful test run exposed `pthread_mutex_lock called on a destroyed mutex` in Android's
`hwuiTask` during native `exit()` teardown. `Sys_Exit` now flushes native streams and uses `_Exit` on Android
to terminate the isolated game process without racing Java/HWUI threads through C++ static destructors;
desktop retains `exit`. Normal Com_Quit still performs engine cleanup first.
On API30+, instrumentation also requires a recorded `ApplicationExitInfo.REASON_EXIT_SELF` with status 0,
so merely printing passing assertions before a native crash is not sufficient.

Verified on the API35 ARM64 emulator: all import transaction checks passed, all runtime modules loaded,
native startup passed 5/5 assertions in 3 smoke tests, and the game process exited with reason EXIT_SELF,
status 0. The corrected log ended after the test summary without FORTIFY errors.
APK signature and ZIP checks passed; every LOAD segment of the ten packaged native libraries
(including the opt-in smoke library) has at least 16 KB alignment. Runtime verification still used 4 KB pages.

### Retail archive verification

The first retail check used the user's local classic 1.26a installation: `war3.mpq` (458500067 bytes),
`War3Patch.mpq` (45620324), `War3x.mpq` (362993137) and `War3xlocal.mpq` (54039425).
There was no separate War3Local.mpq. All four archives opened with the production SFileOpenArchive reader.
`mpqtool ls` could not enumerate the patch archive, so enumeration must not be used as a proxy for
archive validity. A temporary host checker compiled `common/mpq.c` and `vendor/blast/blast.c` unchanged.

For this developer setup, each archive was pushed via adb to a temporary path under `/data/local/tmp/`,
copied with `run-as org.openrealm.wc3` to a staging file in `files/data/`, checked against the host SHA-256,
then renamed to its canonical lowercase name. Temporary adb copies were removed. No original archive
was changed and no MPQ was added to the APK or repository. This verifies developer installation,
not an end-to-end run through the system document picker.

Both editions reached their real main menus on the API35 ARM64 emulator at 1280x800:

- ROC: logo, Russian button text, 3D battlefield, rain, clouds and water were visible. The log also
  recorded navigation to the single-player screen and the five campaign entries. A subsequent fresh ROC
  process also showed the missing-label symptom below; this is not established as TFT-specific.
- TFT: expansion logo, tower, ice, water and particles were visible. Several button labels were blank
  (Battle.net, Options, Credits, Exit), while Single Player and LAN labels were visible. The localized
  MainMenu FDF references KEY_BATTLE_NET/KEY_OPTIONS/KEY_CREDITS/KEY_QUIT, and those values exist in
  War3xlocal's GlobalStrings.fdf. The missing-label cause remains unconfirmed; do not hardcode replacement labels.
- Both bounded 300-frame checks exited with Android EXIT_SELF/status 0. The menu models emitted a
  missing-valid-Birth-duration warning and used the stable pose; neither run logged a shader failure.

The observed screenshot counters were roughly 9–14 FPS with SwiftShader software graphics;
these are not physical Android GPU performance measurements.

Reproduce a bounded launch from a stopped app:

```sh
"$ANDROID_HOME/platform-tools/adb" -s emulator-5554 shell am start -W \
  -n org.openrealm.wc3/.LauncherActivity --ez play true --ei frames 300
# For TFT, additionally pass --ez tft true. frames=0 leaves the app open.
```

The APK opens the edition picker; it has no generated launch profile or automatic map selection.
The launcher consumes the diagnostic `frames` extra for a single launch. Subsequent Play
button launches are unlimited. Previously the extra remained on the launcher's Intent,
causing every subsequent game to exit normally after the same frame count; on Honor this
appeared as repeated returns to the edition picker (`EXIT_SELF`, status 0).

### Physical Honor verification

The developer APK was installed on an Honor AGM3-AL09HN running Android 12 (API31),
ARM64, with 4 KB pages. Its Mali-G52 MC2 reports OpenGL ES 3.2. All four retail archives
were streamed directly into app-private staging files with `adb shell -T run-as ... dd`,
verified against host SHA-256 checksums, then renamed to canonical lowercase names.
This avoids keeping a second archive copy on a device with limited free space.
On this device `adb exec-in run-as ... dd` produced an empty file; verify streaming with
a small fixture and check every archive checksum before committing the staging file.

The real ROC menu and single-player screen rendered successfully without a shader failure.
User navigation also loaded `Maps/Campaign/Prologue01.w3m`; the opening scene rendered
Thrall, terrain, rain and effects. Screenshot counters showed about 52 FPS in the menu
and 13 FPS in the opening scene; these are individual observations, not benchmarks.
Mission completion, audio and touch command behavior were not verified.

This establishes initial production rendering on one Mali device; the standalone bone-127 pixel probe,
Adreno hardware and 16 KB runtime remain untested.

### Remaining validation and limits

The Android main-menu Video resolution popup now selects 3D render scale (100%, 75%, 67%)
instead of SDL desktop modes. Popup callbacks pass a row index, not the stored item value:
`menu_render_scale 0/1/2` maps to 100/75/67 and archives
`r_render_scale`; the existing OK/writeconfig path saves it. Changes apply on the next
3D view. Android startup forces `vid_native=1` and `vid_fullscreen=1` so previously saved
desktop mode selections cannot leave a smaller windowed SDL surface on the tablet.
Android OK writes config without `vid_apply`, avoiding an unnecessary surface resize.
The shared renderer draws large views into a reusable RGBA8 /
depth24 framebuffer and linearly blits the clipped viewport back to the native drawable.
Earlier background layers are copied into the target to preserve transparent menu scenes.
UI, touch coordinates and small portraits remain native. Targets are recreated after resize
and released at 100% and shutdown. Invalid scale values and multisampled drawables use native
rendering; Android currently requests zero MSAA. This does not reduce simulation or animation
work, and FPS improvement requires a same-scene device comparison.
Honor validation: the user selected the popup rows; the fixed build logged 75% as
1440x900 and a subsequent 67% selection as 1286x804, with UI at 1920x1200. Main-menu
text/buttons remained sharp and the 3D background rendered.
The API35 emulator also created a 960x600 view at 75% of its 1280x800 drawable.
The engine `screenshot` command failed to write its relative `screenshots/` path on Android;
adb screencap was used for this visual check. Android screenshot storage remains a follow-up.

The Android runtime enables the `BZ_MUSIC_MP3` streaming backend for retail background
music. Earlier APKs had neither this backend nor FFmpeg and therefore played no music.
The backend uses the existing vendored minimp3 and stereo music mixer, with no retail
assets in the APK. FFmpeg movies and non-MP3 compressed music are not enabled.
On Honor, the updated APK logged `Music: streaming MP3 Sound\Music\mp3Music\Mainscreen.mp3`,
Android reported the game's OpenSL ES media player active, and the user confirmed audible
background music.

Broader campaign loading, audio output, save/load and touch gameplay still require verification.
The intermittent blank-button-label issue above remains open in both editions. APK structural checks, archive-import tests
and native startup tests cannot replace gameplay checks. The shader probe remains available separately.
Background/foreground recovery, context loss, import progress/cancellation across process death,
automatic removal of orphan `.import-*.tmp` files after process death, and additional GPU/page-size tests
remain follow-up work. Quit a game before replacing its archives.

## Engine work remaining

1. Reproduce/fix blank main-menu button labels with a focused regression, then exercise campaign loading, audio and saves.
2. Repeat the production shader APK checks on physical Adreno/Mali devices. `BZ_BONE_PALETTE_MAX` is 128;
   its default-block matrix uniforms can exceed GLES3 minimum limits. Compilation/linking on the emulator
   is not a substitute for physical GPU checks.
3. Finish import progress/cancellation and recovery from process death.
4. Add background/foreground timing and audio policy, and graphics resource restoration.
5. Extend physical-tablet input verification beyond the existing touch selection and two-finger pan.
   Context taps, pinch, long holds, Back and software-keyboard flow remain follow-up work.
6. Check all native libraries and memory-mapping code on 4 KB and 16 KB page-size devices. In particular,
   investigate `FS_MmapFile`'s `MAP_FIXED` at `base + MMAP_HDR_SIZE` (16 bytes) with a regression test before fixing it.

Full Android gameplay, mobile lifecycle recovery and broader physical GPU validation remain pending.

### Direct map diagnostics and loading acknowledgement

The launcher consumes `frames`, `map`, `profile` and `particles` extras once.
`profile=1` selects CPU/particle counters and `profile=2` also attempts GPU timing.
`particles=0/1` disables/enables the particle pass; invalid values and smoke-test launches ignore these overrides.
An optional `map` string extra selects a map explicitly. It forwards a restricted map path
to the ordinary `+map` command, allowing bounded on-device checks without navigating campaign menus:

```sh
adb -s DEVICE shell am start -n org.openrealm.wc3/.LauncherActivity \
  --ez play true --ei frames 20000 --es map Maps/Campaign/Undead05Interlude.w3m
```

Avoid `am start -W` for these checks: on Honor it could continue waiting after the SDL activity was
already visible. Poll `files/engine.log` with `run-as org.openrealm.wc3` instead. Once it logs
`Loading: ready, waiting for acknowledgement`, a fresh touch starts the map. Android enables the
client acknowledgement and server pre-begin simulation gate by default. See
[loading lifecycle and ARM64 interlude continuation](games/warcraft-3/loading-and-assets.md#touch-acknowledgement).
Temporary `com_fast_forward` or debug autoexec files used for diagnostics must be removed afterward.

The JASS type-retention regression extends `wc3_smoke.*` to four tests / six assertions.
`ImportTests` requires that full summary and a clean native exit. On Honor, a diagnostic build
with the old numerical pointer guard failed the enum-copy test (5/6); the fixed VM passes all six assertions and clean-exit checks
through the same instrumentation path.

## Honor performance investigation

The October 16, 2026 device observation was 50–60 FPS in menus and 15–30 FPS in maps at 67% scale
on Mali-G52 MC2, GLES 3.2, with a 1920x1200 drawable, zero MSAA and `r_vsync 0`.
The render-scale output was 1286x804 at
67%: about 45% of native pixel count, with native UI. This reduces fragment work, not
simulation, animation, geometry submission or vertex processing. The builder selects
RelWithDebInfo; `packaging/android/CMakeLists.txt` keeps `-O2 -g` and `-UNDEBUG`;
the Java manifest's `debuggable=true` does not mean unoptimized native code.

These initial observations do not include a weighted Honor CPU/GPU profile. Do not infer a dominant
subsystem from menu/game FPS alone or reuse desktop/Linux sample percentages as Android timings.

### Source-confirmed candidates, not measured FPS gains

| Path | Current work | What a device comparison should decide |
|---|---|---|
| `renderer/r_main.c:R_BeginScaledView` / `R_EndScaledView` | Every eligible scaled view copies native color into the smaller FBO, renders, then blits back. The first copy covers the entire drawable even if the viewport is smaller. No transient-depth invalidation is issued. | Measure the copies and render-target transitions separately. Preserve prior layers for transparent menu scenes; omit the incoming copy only for views whose background is explicitly independent. |
| `w3m/r_war3map.c:_W3M_DrawWorld` | Each `g_groundLayers` buffer submits ground for the whole map; cliffs and water use frustum-culled segments through `R_DrawTerrainSegment`. | Compare submitted vertices and GPU time with visible segment ranges. Keep material ordering and deformation updates; avoid trading all vertex savings for excessive tiny draws. |
| `mdx/r_mdx_geoset.c:MDX_RenderModel` | Solid and alpha passes each call `MDLX_BindBoneMatrices`; attachments and event transforms can evaluate the pose again. | Count pose evaluations per entity/view and profile animation before adding reuse. Keys/lifetimes must include interpolation, global-sequence time and view-dependent billboard transforms. |
| `r_game.c:R_ConformGroundSurfaces` | Each eligible conformer scans the complete view entity list for support surfaces, even when none exist. | Profile this path; the focused collection-pass proposal and required behavior tests are in [WC3 performance](games/warcraft-3/performance.md#september-21-cpu-profile-graveyard-update-scan). |

Paths abbreviated as `w3m/`, `mdx/` and `r_game.c` above belong to
`games/warcraft-3/renderer/`. Whole-map ground batching is deliberate:
`d1647cbd` retains one draw per layer while updating deformed segment slices in place.
It is a draw-count versus submitted-geometry tradeoff, not evidence of an accidental regression.
For render-target handling, [Arm's framebuffer guidance](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-2-how-to-correctly-handle-framebuffers)
explains why avoiding unnecessary loads/stores and invalidating transient attachments before
leaving a render pass matters. Actual savings still require a same-device measurement.

### Controls that currently do less than their labels suggest

`r_model_detail`, `r_anim_quality`, `r_texture_quality` and `r_lights` are registered and
editable in the options UI, but have no renderer consumers in this checkout. Lowering them
does not implement LOD, cheaper animation, a texture mip limit or fewer lights.
`r_particles` is only a boolean check in `R_DrawParticles`: 1 and 2 take the same path;
0 skips particle update/draw there but does not stop emitter work elsewhere.
`r_entities 0` skips `R_DrawEntities`, including solid models and blob shadows, but the
WC3 alpha-model loop in `R_DrawAlphaSurfaces` still runs. Treat it as a partial diagnostic.
`r_fogofwar 0` does not stop server fog simulation or the initial client data update.

### Next on-device capture

Use the same map/save, camera and simulation interval for each comparison. Record scale,
temperature/elapsed run time, `[R_STATS]` draws/vertices/triangles and frame time; repeat
runs so changing campaign content or thermal drift cannot masquerade as an optimization.
Request `com_maxfps 0`, `r_vsync 0` and `r_stats 1` for measurement. Compare
`set r_render_scale 100`, `67` and `50` separately (50 is accepted by the renderer but
is not a popup choice). Restore each setting before testing the next variable.
`r_unit_shadows 0` and `r_particles 0` can isolate parts of the rendering work briefly.
`r_norefresh 1` with `r_stats 1` prints `[R_NOREFRESH] loops=...` while simulation/client
processing continues; this is a main-loop throughput diagnostic, not rendered FPS.

The existing NDK includes Simpleperf. With the game already running in the chosen scene,
this suggested 10-second capture uses the debuggable package without requesting root:

```sh
export ANDROID_SERIAL=DEVICE_SERIAL
export PATH="$PWD/build/android/sdk/platform-tools:$PATH"
perf_root="$PWD"
perf_ndk="$perf_root/build/android/sdk/ndk/28.2.13676358"
mkdir -p build/android/profile
cd build/android/profile
python3 "$perf_ndk/simpleperf/app_profiler.py" -p org.openrealm.wc3 \
  --disable_adb_root --ndk_path "$perf_ndk" \
  -lib "$perf_root/build/android/probe/native" \
  -r '-e task-clock:u -f 1000 -g --duration 10' -o perf.data
python3 "$perf_ndk/simpleperf/report_html.py" -i perf.data -o report.html --no_browser
```

The direct rootless record/report path was verified on Honor (Android 12) on October 16, 2026:

```sh
adb -s DEVICE_SERIAL push "$perf_ndk/simpleperf/bin/android/arm64/simpleperf" /data/local/tmp/simpleperf
adb -s DEVICE_SERIAL shell /data/local/tmp/simpleperf record --app org.openrealm.wc3 \
  -e task-clock:u -f 1000 -g --duration 15 -o /data/local/tmp/openrealm-cpu.data
adb -s DEVICE_SERIAL pull /data/local/tmp/openrealm-cpu.data perf.data
adb -s DEVICE_SERIAL shell /data/local/tmp/simpleperf report -i /data/local/tmp/openrealm-cpu.data \
  --children --sort comm,dso,symbol > report.txt
```

No root or system perf-setting changes were needed; the first steady capture recorded
15,147 samples with no loss. Keep the matching unstripped native libraries/APK for
call-site resolution. The `app_profiler.py` helper above additionally copies symbols;
its full workflow was exercised on the emulator, while the direct commands were used on Honor.
Inspect the native `org.openrealm.wc3:game` process separately from the Java launcher and audio
threads. CPU samples alone cannot measure GPU execution or time blocked in presentation.
The Honor extension log advertises `GL_EXT_disjoint_timer_query`, but extension presence
does not establish valid timings. The current optional asynchronous profiler rejects
disjoint and implausible results; this Honor driver has returned invalid durations.
`r_profile 1` enables CPU/particle counters; `r_profile 2` also attempts GPU timing.
The implementation is in `renderer/r_profile.c`; use `make test-renderer-view test-renderer-frame` for its automated checks.

Before a capture that changes user files, back up both their bytes and whether
each optional file exists. Check presence with `adb shell run-as org.openrealm.wc3
test -f PATH` before reading: on this Honor, `adb exec-out ... cat` returned zero
while its output was `cat: ...: No such file or directory`. That error text is
not a configuration backup. Restore absence with `rm -f`, verify config/map/
campaign/save hashes and keep test overrides out of later ordinary launches.
A live game's diagnostic log may change after restoration; check user data
without restarting a game opened during final verification.

### System frame-pacing capture

On the Honor Android 12 device, `atrace` provides `gfx`, `sched`, `freq`,
`idle` and `sync` categories without root. Capture after the unprofiled FPS
window, with the scene still running:

```sh
adb -s DEVICE_SERIAL shell atrace --list_categories
adb -s DEVICE_SERIAL shell atrace -b 16384 -t 10 \
  -a org.openrealm.wc3,org.openrealm.wc3:game \
  -o /data/local/tmp/openrealm-frames.trace gfx sched freq idle sync
adb -s DEVICE_SERIAL pull /data/local/tmp/openrealm-frames.trace frames.trace
```

Read `sched_switch` slices for the actual native `SDLThread` TID, not the
launcher process. `tracing_mark_write` B/E markers expose EGL swaps and nested
buffer-queue calls; their durations are wall time, not GPU timers. Counter
events and nested spans must not be summed as independent work. The bounded
command stops tracing on exit; verify `debug.atrace.tags.enableflags` is 0
afterward. Keep trace-enabled captures separate from primary FPS measurements.

`dumpsys SurfaceFlinger --timestats -dump` contains cumulative data from older
runs under the same layer name. Preserve snapshots and use deltas rather than
its raw `averageFPS`; do not clear prior diagnostics. `--timestats -enable` /
`-disable` control collection. The `run-as ... taskset -p ...` scheduler call
returns permission errors on this Honor, and MTK GPU frequency/utilization
files under `/sys/kernel/ged/hal/` and `/proc/gpufreq/` are also unreadable.
The trace's CPU-frequency events and scheduler slices remain usable.

On this Honor, the native process working directory is `/`; the renderer's
relative `screenshots/shotNNNN.jpg` write fails there. Physical framebuffer QA
therefore used `adb exec-out screencap -p` after the measured window. Do not
include screenshot/readback time in the FPS measurement.
