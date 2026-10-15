#!/usr/bin/env python3
"""Publish runtime browser assets to the existing MkDocs Pages artifact.

Do not publish unity-generated C, compiler object files, or proprietary MPQs.
"""
from pathlib import Path
import shutil
import sys

# The browser build emits its runtime into a flat directory today. Preserve
# subdirectories for future runtime assets without publishing intermediate code.
RUNTIME_SUFFIXES = {
    '.html', '.js', '.wasm', '.data', '.css', '.json', '.mem',
    '.png', '.jpg', '.jpeg', '.webp', '.svg', '.ico',
    '.woff', '.woff2', '.ttf', '.ogg', '.mp3',
}
REQUIRED = ('index.html', 'index.js', 'index.wasm',
            'filesystem-bridge.js', 'installer.js')
PAGES_ASSETS = ('pages-isolation.js', 'coi-serviceworker.js')


def publish(source, destination, site_assets):
    for name in REQUIRED:
        if not (source / name).is_file():
            raise RuntimeError('missing browser runtime asset: ' + name)
    if destination.is_symlink():
        raise RuntimeError('Pages destination must not be a symlink')
    if destination.exists():
        if not destination.is_dir():
            raise RuntimeError('Pages destination must be a directory')
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    count = 0
    for asset in source.rglob('*'):
        if not asset.is_file() or asset.is_symlink() or asset.suffix.lower() not in RUNTIME_SUFFIXES:
            continue
        rel = asset.relative_to(source)
        target = destination / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(asset, target)
        count += 1
    for name in PAGES_ASSETS:
        asset = site_assets / name
        if not asset.is_file():
            raise RuntimeError('missing Pages isolation asset: ' + name)
        shutil.copyfile(asset, destination / name)
    print(f'Published {count} runtime assets and Pages isolation scripts to {destination}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: publish-pages.py <build/web/engine> <site/play>')
    repo = Path(__file__).resolve().parents[2]
    publish(Path(sys.argv[1]), Path(sys.argv[2]), repo / 'port/web/site')
