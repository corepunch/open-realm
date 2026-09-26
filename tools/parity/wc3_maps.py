#!/usr/bin/env python3
"""Build an MPQ-backed campaign catalog and select maps without launching a game."""
import argparse
import configparser
import csv
import curses
import json
import os
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from wc3_map_audit import clean_title, parse_w3i_name
from map_archive import read_member
from map_picker import label, filter_maps, picker_theme, draw_picker, pick_map

# Ordered from base data to localized/expansion data and finally the patch archive.
ARCHIVES = ('war3.mpq', 'war3local.mpq', 'war3x.mpq', 'war3xlocal.mpq', 'war3patch.mpq')
CAMPAIGN_FILES = {'roc': 'UI/CampaignStrings.txt', 'tft': 'UI/CampaignStrings_exp.txt'}
CAMPAIGN_DIRS = {'roc': 'Maps/Campaign/', 'tft': 'Maps/FrozenThrone/Campaign/'}
PREFIXES = {'nightelf': 'elf', 'human': 'human', 'undead': 'undead', 'orc': 'orc', 'prologue': 'prologue'}
MENU = dict(edition='tft', slug='menu', name='Main Menu', title='', campaign='', path='')


def map_slug(stem):
    match = re.fullmatch(r'(NightElf|Human|Undead|Orc|Prologue)X?0*(\d+)(.*)', stem, re.I)
    if not match:
        return re.sub(r'[^a-z0-9]+', '-', stem.lower()).strip('-')
    family, number, suffix = match.groups()
    suffix = re.sub(r'^(Interlude|Finale|Secret)', r'-\1', suffix, flags=re.I)
    suffix = re.sub(r'_0*(\d+)', r'-\1', suffix)
    return PREFIXES[family.lower()] + str(int(number)) + suffix.lower()


def csv_fields(value):
    return next(csv.reader([value], skipinitialspace=True)) or ['']


def parse_campaign(text, edition):
    parser = configparser.ConfigParser(interpolation=None, comment_prefixes=('//', ';'))
    parser.read_string(text.lstrip('\ufeff'))
    rows = []
    for section in parser.sections():
        table = parser[section]
        for key in sorted((k for k in table if re.fullmatch(r'mission\d+', k)), key=lambda k: int(k[7:])):
            number = key[7:]
            if 'file' + number in table:
                path = CAMPAIGN_DIRS[edition] + csv_fields(table['file' + number])[0] + '.w3m'
                title, name = csv_fields(table.get('title' + number, ''))[0], csv_fields(table[key])[0]
            else:
                title, name, path, *_ = csv_fields(table[key])
                path = path.replace('\\', '/')
            if not path.lower().endswith(('.w3m', '.w3x')):
                print(f'wc3 catalog: non-map cinematic {name}: {path}', file=sys.stderr)
                continue
            rows.append(dict(edition=edition, slug=map_slug(Path(path).stem), name=clean_title(name),
                             title=clean_title(title), campaign=csv_fields(table.get('name', section))[0], path=path))
    return rows


def script_maps(script):
    # Literal next-level paths plus the bonus campaign's authored zone table.
    literals = re.findall(r'"([^"\r\n]*)"', script)
    paths = {s.replace('\\\\', '/').replace('\\', '/') for s in literals if s.lower().endswith(('.w3m', '.w3x'))}
    zone = {}
    schema = {'udg_ZoneMapPath': 'path', 'udg_ZoneMapExt': 'ext', 'udg_ZoneMaps': 'maps'}
    for field, index, value in re.findall(r'set\s+(\w+)(?:\[(\d+)\])?\s*=\s*"([^"\r\n]*)"', script):
        if field not in schema or not value:
            continue
        key = schema[field]
        if key == 'maps':
            zone.setdefault(key, []).append(value)
        else:
            zone[key] = value.replace('\\\\', '/').replace('\\', '/')
    if zone.get('maps'):
        if not zone.get('path') or not zone.get('ext'):
            raise ValueError('bonus campaign zone table lacks path or extension')
        paths.update(zone['path'].rstrip('/') + '/' + name + zone['ext'] for name in zone['maps'])
    return sorted(path for path in paths if path.lower().startswith('maps/'))


def decode_text(data, source):
    try:
        return data.decode('utf-8-sig')
    except UnicodeDecodeError:
        print(f'wc3 catalog: decoding legacy Windows-1252 text: {source}', file=sys.stderr)
        return data.decode('cp1252')


def find_archives(data):
    found = {}
    for directory in [data, data / 'Frozen Throne']:
        if directory.is_dir():
            for path in directory.iterdir():
                if path.name.lower() in ARCHIVES and path.is_file():
                    found[path.name.lower()] = path
    if 'war3.mpq' not in found:
        raise RuntimeError(f'{data}: missing War3.mpq')
    return [found[name] for name in ARCHIVES if name in found]


def generate_catalog(archives, tool):
    members, authored = {}, {}
    for archive in archives:
        listing = read_member(tool, archive, '(listfile)', optional=True)
        if listing is None:
            print(f'wc3 catalog: {archive.name} has no listfile; using campaign and script references', file=sys.stderr)
        else:
            for path in listing.decode('utf-8-sig').splitlines():
                path = path.replace('\\', '/').strip()
                if path.lower().startswith(tuple(p.lower() for p in CAMPAIGN_DIRS.values())) and path.lower().endswith(('.w3m', '.w3x')):
                    members[path.lower()] = path
        for edition, path in CAMPAIGN_FILES.items():
            text = read_member(tool, archive, path, optional=True)
            if text is not None:
                authored[edition] = parse_campaign(decode_text(text, f'{archive}:{path}'), edition)
    if not authored:
        raise RuntimeError('No campaign tables found in the installation')
    rows = {row['path'].lower(): row for group in authored.values() for row in group}
    members.update({key: row['path'] for key, row in rows.items()})
    done = set()
    while pending := sorted(set(members) - done):
        for key in pending:
            path = members[key]
            payload = None
            for archive in reversed(archives):
                payload = read_member(tool, archive, path, optional=True)
                if payload is not None:
                    break
            if payload is None:
                raise RuntimeError(f'Campaign map referenced but missing: {path}')
            with tempfile.NamedTemporaryFile(suffix=Path(path).suffix) as nested:
                nested.write(payload)
                nested.flush()
                if key not in rows:
                    info = read_member(tool, nested.name, 'war3map.w3i')
                    wts = read_member(tool, nested.name, 'war3map.wts', optional=True)
                    name = parse_w3i_name(info, decode_text(wts or b'', path + ':war3map.wts'), '')
                    if not name or 'TRIGSTR_' in name:
                        raise RuntimeError(f'Unresolved map title: {path}: {name}')
                    edition = 'tft' if path.lower().startswith(CAMPAIGN_DIRS['tft'].lower()) else 'roc'
                    rows[key] = dict(edition=edition, slug=map_slug(Path(path).stem), name=name,
                                     title='', campaign='Additional campaign maps', path=path)
                script = read_member(tool, nested.name, 'war3map.j', optional=True)
                if script is None:
                    script = read_member(tool, nested.name, 'Scripts/war3map.j', optional=True)
                if script is not None:
                    for reference in script_maps(decode_text(script, path + ':war3map.j')):
                        members.setdefault(reference.lower(), reference)
            done.add(key)
    result = sorted(rows.values(), key=lambda row: (row['edition'], row['path'].lower()))
    aliases = [(row['edition'], row['slug']) for row in result]
    if len(set(aliases)) != len(aliases):
        raise ValueError('Duplicate campaign map aliases')
    return result


def load_catalog(data, tool, refresh=False, map_dirs=()):
    if override := os.environ.get('WC3_MAP_CATALOG'):
        return json.loads(Path(override).read_text())['maps']
    data = data.expanduser().resolve()
    archives = find_archives(data)
    loose = {}
    for directory in map_dirs:
        directory = directory.expanduser().resolve()
        if not directory.is_dir():
            raise ValueError(f'Map directory does not exist: {directory}')
        for path in sorted(directory.rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.w3m', '.w3x'):
                loose[path] = directory
    signature = [(str(p.resolve()), p.stat().st_size, p.stat().st_mtime_ns) for p in [*archives, *loose, tool, Path(__file__)]]
    signature = dict(files=json.loads(json.dumps(signature)), map_dirs=[str(p.expanduser().resolve()) for p in map_dirs])
    cache = ROOT / 'build/parity/campaign-maps.json'
    if cache.exists() and not refresh:
        saved = json.loads(cache.read_text())
        if saved.get('signature') == signature:
            return saved['maps']
    print('wc3 catalog: reading campaign maps from MPQ archives...', file=sys.stderr)
    rows = generate_catalog(archives, tool)
    for path, directory in loose.items():
        info = read_member(tool, path, 'war3map.w3i')
        wts = read_member(tool, path, 'war3map.wts', optional=True)
        name = parse_w3i_name(info, decode_text(wts or b'', str(path)), '')
        if not name or 'TRIGSTR_' in name:
            raise ValueError(f'Unresolved map title: {path}: {name}')
        slug = 'custom-' + re.sub(r'[^a-z0-9]+', '-', (directory.name + '/' + str(path.relative_to(directory).with_suffix(''))).lower()).strip('-')
        rows.append(dict(edition='tft' if path.suffix.lower() == '.w3x' else 'roc', slug=slug,
                         name=name, title='', campaign=f'Custom maps: {directory.name}',
                         path=os.path.relpath(path, data)))
    if len({(row['edition'], row['slug']) for row in rows}) != len(rows):
        raise ValueError('Duplicate map aliases; give optional map folders distinct names')
    cache.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=cache.parent, delete=False) as output:
        json.dump(dict(signature=signature, maps=rows), output, indent=2, ensure_ascii=False)
        output.write('\n')
    os.replace(output.name, cache)
    print(f'wc3 catalog: {len(rows)} maps saved to {cache}', file=sys.stderr)
    return rows


def resolve_map(rows, slug, edition):
    for prefix in CAMPAIGN_FILES:
        if slug.startswith(prefix + '-'):
            edition, slug = prefix, slug[len(prefix) + 1:]
            break
    if slug == 'menu':
        return dict(MENU, edition=edition)
    for row in rows:
        if row['edition'] == edition and row['slug'] == slug:
            return row
    raise ValueError(f"Unknown map '{slug}' in {edition}; use --list or --select")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--mpqtool', type=Path, default=ROOT / 'build/bin/mpqtool')
    parser.add_argument('--edition', choices=CAMPAIGN_FILES, default='tft')
    parser.add_argument('--refresh', action='store_true')
    parser.add_argument('--maps-dir', type=Path, action='append', default=[])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--list', action='store_true')
    action.add_argument('--select', action='store_true')
    action.add_argument('--resolve')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        rows = load_catalog(args.data, args.mpqtool, args.refresh, args.maps_dir)
        if args.list:
            for row in [MENU, *rows]:
                print(label(row))
            return 0
        if args.select:
            if not sys.stdin.isatty() or not sys.stdout.isatty():
                raise ValueError('--select needs an interactive terminal; use --list otherwise')
            row = curses.wrapper(pick_map, [dict(MENU, edition=args.edition), *rows])
            if row is None:
                return 130
        else:
            row = resolve_map(rows, args.resolve, args.edition)
        value = row['edition'] + '\t' + row['path'] + '\n'
        if args.output:
            args.output.write_text(value)
        else:
            print(value, end='')
        return 0
    except (OSError, ValueError, RuntimeError, configparser.Error, curses.error) as error:
        print(f'wc3 catalog: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
