#!/usr/bin/env python3
"""Build the SDL2/GLES3 probe or full WC3 debug APK using CMake and Android SDK tools."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = "org.openrealm.probe"


def run(args, **kwargs):
    args = list(map(str, args))
    print("+", shlex.join(args), flush=True)
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=os.environ.get("ANDROID_HOME", ROOT / "build/android/sdk"))
    parser.add_argument("--sdl", default=ROOT / "build/android/SDL2-2.32.10")
    parser.add_argument("--ndk-version", default="28.2.13676358")
    parser.add_argument("--out")
    parser.add_argument("--game", action="store_true", help="build the full WC3 runtime and MPQ importer")
    parser.add_argument("--serial", help="install/launch on this adb device; the probe also requires RESULT PASS")
    args = parser.parse_args()
    sdk, sdl, out = (Path(value).resolve() for value in
        (args.sdk, args.sdl, args.out or ROOT / ("build/android/game" if args.game else "build/android/probe")))
    ndk = sdk / "ndk" / args.ndk_version
    tools = sdk / "build-tools/35.0.1"
    android_jar = sdk / "platforms/android-35/android.jar"
    java_home = os.environ.get("JAVA_HOME")
    if not java_home:
        parser.error("Set JAVA_HOME to a JDK (javac and keytool are required)")
    java = Path(java_home) / "bin"
    for path in (ndk / "build/cmake/android.toolchain.cmake", sdl / "CMakeLists.txt", android_jar,
                 tools / "aapt2", tools / "d8", tools / "apksigner", tools / "zipalign", java / "javac"):
        if not path.is_file():
            parser.error(f"Missing build input: {path}; see docs/android.md")
    out.mkdir(parents=True, exist_ok=True)
    # Share the SDL/native build tree between the independently packaged probe and game APKs.
    native = ROOT / "build/android/probe/native"
    run(["cmake", "-S", ROOT / "packaging/android", "-B", native, "-G", "Unix Makefiles",
         f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake", "-DANDROID_ABI=arm64-v8a",
         "-DANDROID_PLATFORM=android-26", "-DCMAKE_BUILD_TYPE=RelWithDebInfo", f"-DSDL_SOURCE_DIR={sdl}",
         f"-DBZ_ANDROID_GAME={'ON' if args.game else 'OFF'}"])
    run(["cmake", "--build", native, "--target", *( ["warcraft3", "mpqcheck"] if args.game else ["main"] ),
         "--parallel", "3"])
    classes, dex = out / "classes", out / "dex"
    classes.mkdir(exist_ok=True)
    dex.mkdir(exist_ok=True)
    sources = sorted((sdl / "android-project/app/src/main/java").rglob("*.java"))
    sources.extend((ROOT / "packaging/android" / name) for name in
        (["ArchiveStore.java", "LauncherActivity.java", "GameActivity.java", "ImportTests.java"] if args.game else ["ProbeActivity.java"]))
    # Android's stub LambdaMetafactory lacks the compiler methods; SDK build tools supplies them.
    boot = os.pathsep.join(map(str, (tools / "core-lambda-stubs.jar", android_jar)))
    run([java / "javac", "-source", "8", "-target", "8", "-Xlint:-options", "-bootclasspath", boot,
         "-d", classes, *sources])
    jar = out / "classes.jar"
    with zipfile.ZipFile(jar, "w") as archive:
        for path in sorted(classes.rglob("*.class")):
            archive.write(path, path.relative_to(classes))
    run([tools / "d8", "--lib", android_jar, "--min-api", "26", "--output", dex, jar])
    unsigned = out / "unsigned.apk"
    manifest = ROOT / "packaging/android" / ("GameManifest.xml" if args.game else "AndroidManifest.xml")
    run([tools / "aapt2", "link", "-I", android_jar, "--manifest", manifest,
         "-o", unsigned])
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(dex / "classes.dex", "classes.dex")
        libraries = [("libSDL2.so", native / "sdl/libSDL2.so")]
        for name in (["warcraft3", "realm_shared", "realm_sheet", "realm_jass", "realm_renderer", "realm_game", "realm_menu", "realm_smoke", "mpqcheck"]
                     if args.game else ["main"]):
            libraries.append((f"lib{name}.so", native / f"lib{name}.so"))
        for name, source in libraries:
            apk.write(source, f"lib/arm64-v8a/{name}")
        if args.game:
            for source_dir, target in ((ROOT / "share", "share"), (ROOT / "games/warcraft-3/share", "share/warcraft-3")):
                for path in sorted(source_dir.rglob("*")):
                    if path.is_file():
                        apk.write(path, f"assets/{target}/{path.relative_to(source_dir).as_posix()}")
    aligned = out / "aligned.apk"
    run([tools / "zipalign", "-f", "-P", "16", "4", unsigned, aligned])
    key = out / "debug.keystore"
    if not key.exists():
        run([java / "keytool", "-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android",
             "-alias", "androiddebugkey", "-dname", "CN=OpenRealm Android Debug", "-keyalg", "RSA",
             "-validity", "10000"])
    apk = out / ("openrealm-warcraft3.apk" if args.game else "openrealm-probe.apk")
    run([tools / "apksigner", "sign", "--ks", key, "--ks-pass", "pass:android", "--out", apk, aligned])
    run([tools / "apksigner", "verify", "--verbose", apk])
    run([tools / "zipalign", "-c", "-P", "16", "4", apk])
    print(f"APK ready: {apk}", flush=True)
    if not args.serial:
        return
    adb = [sdk / "platform-tools/adb", "-s", args.serial]
    run([*adb, "install", "--no-incremental", "-r", apk])
    if args.game:
        run([*adb, "shell", "am", "start", "-W", "-n", "org.openrealm.wc3/.LauncherActivity"])
        return
    run([*adb, "shell", "am", "force-stop", PACKAGE])
    run([*adb, "shell", "am", "start", "-W", "-n", PACKAGE + "/.ProbeActivity"])
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        pid = run([*adb, "shell", "pidof", PACKAGE], capture_output=True, text=True).stdout.strip()
        logs = run([*adb, "logcat", "-d", f"--pid={pid}", "-s", "OpenRealmProbe:I", "*:S"],
                   capture_output=True, text=True).stdout
        if "RESULT " in logs:
            (out / "probe-logcat.txt").write_text(logs)
            print(logs, flush=True)
            with (out / "probe.png").open("wb") as screenshot:
                run([*adb, "exec-out", "screencap", "-p"], stdout=screenshot)
            if "RESULT PASS" not in logs:
                raise SystemExit("Production shader probe failed; see probe-logcat.txt and logcat")
            return
        time.sleep(1)
    raise SystemExit("Timed out waiting for shader results; inspect adb logcat")


if __name__ == "__main__":
    main()
