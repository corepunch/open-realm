#!/usr/bin/env python3
"""Package OpenWarcraft3 for iPad, using only SDK tools (no Xcode project)."""
import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess

FRAMEWORKS = ('shared', 'jass', 'sheet', 'renderer', 'game', 'menu')


def framework_plist(name, bundle_id):
    return {
        'CFBundleIdentifier': bundle_id + '.' + name,
        'CFBundleExecutable': name,
        'CFBundleName': name,
        'CFBundleDisplayName': name,
        'CFBundleDevelopmentRegion': 'en',
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundlePackageType': 'FMWK',
        'CFBundleShortVersionString': '1.0',
        'CFBundleVersion': '1',
        'MinimumOSVersion': '16.0',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'target', 'binary', 'frameworks'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--bundle-id', required=True)
    parser.add_argument('--sdk', choices=('iphoneos', 'iphonesimulator'), required=True)
    parser.add_argument('--sdk-version', required=True)
    parser.add_argument('--minimum', required=True)
    args = parser.parse_args()
    root, target = args.root.resolve(), args.target.resolve()
    if target.suffix != '.app' or target == root or target in root.parents:
        raise SystemExit('Target must be a separate .app bundle')
    icon = root / 'packaging/ipad/icons' / 'warcraft3.png'
    if not icon.is_file():
        raise SystemExit('Missing app icon: ' + str(icon))
    for name in FRAMEWORKS:
        if not (args.frameworks / (name + '.framework') / name).is_file():
            raise SystemExit('Missing framework binary: ' + name)
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    # Embedded frameworks: the desktop build keeps game/menu/renderer/jass/
    # sheet/shared as separate shared libraries (each with its own FDF parser
    # copy); iOS mirrors that with .framework bundles under Frameworks/.
    frameworks_dir = target / 'Frameworks'
    frameworks_dir.mkdir()
    for name in FRAMEWORKS:
        dest = frameworks_dir / (name + '.framework')
        shutil.copytree(args.frameworks / (name + '.framework'), dest)
        (dest / 'Info.plist').write_bytes(plistlib.dumps(framework_plist(name, args.bundle_id)))
    shutil.copytree(root / 'share', target / 'share')
    wc3_share = root / 'games/warcraft-3/share'
    if wc3_share.is_dir():
        shutil.copytree(wc3_share, target / 'share/warcraft-3', dirs_exist_ok=True)
    shutil.copy2(args.binary, target / 'warcraft3')
    catalog = target.parent / 'AppIcon.xcassets'
    appicons = catalog / 'AppIcon.appiconset'
    appicons.mkdir(parents=True, exist_ok=True)
    images = []
    for size, scale in ((20, 1), (20, 2), (29, 1), (29, 2), (40, 1), (40, 2), (76, 1), (76, 2), (83.5, 2), (1024, 1)):
        pixels = int(size * scale)
        filename = f'icon-{pixels}.png'
        output = appicons / filename
        if not output.exists() or output.stat().st_mtime < icon.stat().st_mtime:
            subprocess.run(['sips', '-z', str(pixels), str(pixels), str(icon), '--out', str(output)], check=True, stdout=subprocess.DEVNULL)
        images.append({'idiom': 'ios-marketing' if size == 1024 else 'ipad', 'size': f'{size}x{size}', 'scale': f'{scale}x', 'filename': filename})
    (appicons / 'Contents.json').write_text(json.dumps({'images': images, 'info': {'version': 1, 'author': 'OpenWarcraft3'}}, indent=2))
    partial = target.parent / 'icon-info.plist'
    subprocess.run(['xcrun', '--sdk', args.sdk, 'actool', str(catalog), '--compile', str(target), '--output-partial-info-plist', str(partial), '--app-icon', 'AppIcon', '--target-device', 'ipad', '--minimum-deployment-target', args.minimum, '--platform', args.sdk, '--output-format', 'human-readable-text'], check=True)
    info = plistlib.loads(partial.read_bytes())
    # NOTE: no UIApplicationSceneManifest here. SDL2 provides its own
    # UIApplicationDelegate; an Orion-style AXSceneDelegate entry would
    # hijack scene creation from SDL and break startup.
    info.update({
        'CFBundleIdentifier': args.bundle_id,
        'CFBundleExecutable': 'warcraft3',
        'CFBundleName': 'warcraft3',
        'CFBundleDisplayName': 'OpenWarcraft3',
        'CFBundleDevelopmentRegion': 'en',
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': '1.0',
        'CFBundleVersion': '1',
        'UIDeviceFamily': [2],
        'MinimumOSVersion': args.minimum,
        'CFBundleSupportedPlatforms': ['iPhoneOS' if args.sdk == 'iphoneos' else 'iPhoneSimulator'],
        'DTPlatformName': args.sdk,
        'DTPlatformVersion': args.sdk_version,
        'DTSDKName': args.sdk + args.sdk_version,
        'LSRequiresIPhoneOS': True,
        'UIRequiredDeviceCapabilities': ['opengles-3'],
        'UIFileSharingEnabled': True,
        'LSSupportsOpeningDocumentsInPlace': True,
        'UILaunchScreen': {},
        'UIRequiresFullScreen': True,
        # SDL mouse/trackpad support (iPad pointer, Magic Keyboard trackpad).
        'UIApplicationSupportsIndirectInputEvents': True,
        'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait', 'UIInterfaceOrientationPortraitUpsideDown', 'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
    })
    (target / 'Info.plist').write_bytes(plistlib.dumps(info))
    (target / 'PkgInfo').write_bytes(b'APPL????')
    print('Packaged ' + str(target))


if __name__ == '__main__':
    main()
