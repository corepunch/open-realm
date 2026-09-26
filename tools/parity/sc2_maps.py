#!/usr/bin/env python3
"""List and select installed StarCraft II maps without launching the game."""

import argparse
import curses
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from functools import partial

from map_archive import read_member
from map_picker import label, pick_map

ROOT = Path(__file__).resolve().parents[2]
MAP_MEMBER = re.compile(r'(?i)^(maps/.+?\.sc2map)/(?:MapInfo|MapInfo\.xml)$')
LOOSE_EXTS = ('.sc2map', '.sc2components')


def map_slug(path):
    return re.sub(r'[^a-z0-9]+', '-', Path(path).stem.lower()).strip('-')


def authored_name(strings, path):
    if strings is None:
        print(f'sc2 catalog: missing enUS GameStrings for {path}; using map identifier', file=sys.stderr)
        return Path(path).stem
    for line in strings.decode('utf-8-sig').splitlines():
        if line.startswith('DocInfo/Name='):
            value = line.partition('=')[2].strip()
            if value:
                return value
    print(f'sc2 catalog: missing DocInfo/Name for {path}; using map identifier', file=sys.stderr)
    return Path(path).stem


def archives_in(data):
    return sorted((path for path in data.rglob('*') if path.is_file() and path.suffix.lower() == '.sc2maps'),
                  key=lambda path: str(path).lower())


def loose_maps_in(data):
    found = {path.resolve() for path in data.rglob('*') if path.suffix.lower() in LOOSE_EXTS and
             (path.is_file() or (path.is_dir() and
              ((path / 'MapInfo').is_file() or (path / 'MapInfo.xml').is_file())))}
    return sorted(found, key=lambda path: str(path).lower())


def generate_catalog(data, tool, archives, loose):
    rows = {}
    sources = {}
    for archive in archives:
        listing = read_member(tool, archive, '(listfile)', optional=True)
        if listing is None:
            raise RuntimeError(f'{archive}: missing (listfile); cannot discover maps')
        paths = {match.group(1) for line in listing.decode('utf-8-sig').splitlines()
                 if (match := MAP_MEMBER.match(line.replace('\\', '/').strip()))}
        for path in sorted(paths):
            if path.lower() in rows:
                raise ValueError(f'Duplicate map path {path} in {sources[path.lower()]} and {archive}')
            strings = read_member(tool, archive, path + '/enUS.SC2Data/LocalizedData/GameStrings.txt', optional=True)
            rows[path.lower()] = dict(edition='sc2', slug=map_slug(path), name=authored_name(strings, path),
                                      title='', campaign=archive.parent.name, path=path)
            sources[path.lower()] = archive
    for folder in loose:
        path = folder.relative_to(data).as_posix()
        if path.lower() in rows:
            print(f'sc2 catalog: loose map {folder} is shadowed by {sources[path.lower()]}', file=sys.stderr)
            continue
        if folder.is_file():
            if read_member(tool, folder, 'MapInfo', optional=True) is None:
                read_member(tool, folder, 'MapInfo.xml')
            strings = read_member(tool, folder, 'enUS.SC2Data/LocalizedData/GameStrings.txt', optional=True)
        else:
            strings_path = folder / 'enUS.SC2Data/LocalizedData/GameStrings.txt'
            strings = strings_path.read_bytes() if strings_path.is_file() else None
        rows[path.lower()] = dict(edition='sc2', slug=map_slug(path), name=authored_name(strings, path),
                                  title='', campaign='Loose maps', path=path)
    result = sorted(rows.values(), key=lambda row: row['path'].lower())
    slugs = [row['slug'] for row in result]
    if len(slugs) != len(set(slugs)):
        raise ValueError('Duplicate map aliases; use an explicit --map path for these maps')
    return result


def load_catalog(data, tool, refresh=False):
    if override := os.environ.get('SC2_MAP_CATALOG'):
        return json.loads(Path(override).read_text())['maps']
    data = data.expanduser().resolve()
    if not data.is_dir():
        raise ValueError(f'Missing StarCraft II data directory: {data}')
    archives = archives_in(data)
    loose = loose_maps_in(data)
    if not archives and not loose:
        raise ValueError(f'No .SC2Maps archives or loose SC2 maps under {data}')
    loose_metadata = [file for folder in loose if folder.is_dir() for file in
                      (folder / 'MapInfo', folder / 'MapInfo.xml',
                       folder / 'enUS.SC2Data/LocalizedData/GameStrings.txt')
                      if file.is_file()]
    signature = [(str(path), path.stat().st_size, path.stat().st_mtime_ns)
                 for path in [*archives, *loose, *loose_metadata, tool, Path(__file__)]]
    cache = ROOT / 'build/parity/sc2-maps.json'
    if cache.exists() and not refresh:
        saved = json.loads(cache.read_text())
        if saved.get('signature') == signature:
            return saved['maps']
    rows = generate_catalog(data, tool, archives, loose)
    cache.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=cache.parent, delete=False) as output:
        json.dump(dict(signature=signature, maps=rows), output, indent=2, ensure_ascii=False)
        output.write('\n')
    os.replace(output.name, cache)
    print(f'sc2 catalog: {len(rows)} maps saved to {cache}', file=sys.stderr)
    return rows


def resolve_map(rows, slug):
    matches = [row for row in rows if row['slug'] == slug]
    if len(matches) == 1:
        return matches[0]
    raise ValueError(f"Unknown map '{slug}'; use --list or --select")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--mpqtool', type=Path, default=ROOT / 'build/bin/mpqtool')
    parser.add_argument('--refresh', action='store_true')
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--list', action='store_true')
    action.add_argument('--select', action='store_true')
    action.add_argument('--resolve')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        rows = load_catalog(args.data, args.mpqtool, args.refresh)
        if args.list:
            for row in rows:
                print(label(row))
            return 0
        if args.select:
            if not sys.stdin.isatty() or not sys.stdout.isatty():
                raise ValueError('--select needs an interactive terminal; use --list otherwise')
            row = curses.wrapper(partial(pick_map, rows=rows,
                                         heading='S T A R C R A F T   I I', subtitle='MAP ATLAS',
                                         compact_heading='STARCRAFT II'))
            if row is None:
                return 130
        else:
            row = resolve_map(rows, args.resolve)
        value = row['path'] + '\n'
        if args.output:
            args.output.write_text(value)
        else:
            print(value, end='')
        return 0
    except (OSError, ValueError, RuntimeError, UnicodeError, curses.error) as error:
        print(f'sc2 catalog: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
