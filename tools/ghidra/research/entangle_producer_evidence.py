"""Literal Entangle admission/placement witnesses; no complete trajectory claim."""
from collections import Counter
import gzip
import json
import re
import struct

from itemdrop_evidence import ROOT, SHA, digest, literal, require

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-entangle-producer-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/entangle-producer'
NAMES = ('itemdrop_capture.py', 'itemdrop_make_map.py', 'itemdrop_observer.js', 'itemdrop_probe.j')
MAP_SHA = '2b71c38980104f0fe256bfcc24521f5865fe5d2646ce03c41df526651c279eb7'
CONTROL = {'metadata', 'owned-loading-input', 'owned-loading-input-skipped', 'loading-key',
           'control-start-file', 'preload-file'}


def word(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def public_contract(markers):
    require(len(markers) == 187 and markers[-1] == 'TEN tick=3999 scene=4 label=complete',
            'incomplete Entangle public stream')
    rows = [dict(token.split('=', 1) for token in value.split()[1:]) for value in markers]
    for scene in range(4):
        part = [row for row in rows if row['scene'] == str(scene)]
        before = [row for row in part if row['label'] == 'before']
        issue = [row for row in part if row['label'] == 'entangle-return']
        end = [row for row in part if row['label'] == 'settled']
        require(len(before) == len(issue) == len(end) == 1, 'missing Entangle admission/completion')
        require(before[0]['head'] == '0', 'Entangle issued before unroot completed')
        require(issue[0]['accepted'] == '1' and issue[0]['head'] == ('0' if scene == 0 else '852147'),
                'Entangle public admission/head differs')
        require(end[0]['head'] == '0' and end[0]['minevisible'] == '0' and end[0]['overlays'] == '1',
                'Entangle terminal overlay differs')
        require((end[0]['x'], end[0]['y']) == (('896.000', '960.000') if scene % 2 else ('704.000', '1024.000')),
                'Entangle final placement differs')


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False, 'wrong Entangle evidence contract')
    require(digest(literal(data['map'], 'raw')) == MAP_SHA, 'Entangle map changed')
    require(len(data['captures']) == 3, 'two Entangle observations and a control required')
    streams = []
    for index, capture in enumerate(data['captures']):
        rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
        preload = literal(capture, 'preload')
        header, footer = rows[0], rows[-1]
        mode = 'control' if index == 2 else 'observe'
        require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                header['sha256'] == SHA and header['task'] == 'TARGET-01.2-entangle' and
                header['prefix'] == 'TEN ', 'wrong owned Entangle capture')
        env = header['env']
        require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                'wrong Entangle research environment')
        for name in NAMES:
            require(header['source_sha256'][name] == digest((SOURCES / name).read_bytes()),
                    'Entangle source changed ' + name)
        require(header['source_sha256']['map'] == MAP_SHA, 'captured Entangle map changed')
        require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                'failed Entangle observer')
        markers = re.findall(r'call Preload\( "(TEN [^"\r\n]*)" \)', preload.decode())
        public_contract(markers)
        require(footer == dict(event='preload-file', sha256=digest(preload), markers=187, complete=True),
                'incomplete Entangle preload')
        if mode == 'observe':
            require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                    ['TEN tick=0 scene=-1 label=start'] + markers, 'Entangle observer/preload differs')
            ends = [row for row in rows if row.get('event') == 'trace-end']
            require(len(ends) == 1 and ends[0]['installed'], 'missing Entangle observer termination')
            counts = ends[0]['counts']
            require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                    'Entangle observer counts differ')
            for scene in range(4):
                part = [row for row in rows if row.get('scene') == scene]
                targets = [row for row in part if row['event'] == 'target-task']
                require(len(targets) == 1 and targets[0]['code'] == 0xd018b and targets[0]['range'] == 0,
                        'Entangle retained target action differs')
                points = [row for row in part if row['event'] == 'point-task']
                require([row['code'] for row in points] ==
                        ([] if scene == 0 else [0xd018f] if scene == 2 else [0xd016b, 0xd018f]),
                        'Entangle point/facing branch differs')
                for point in points:
                    require(point['range'] == 0 and point['point'] ==
                            ([word(704), word(1024)] if scene == 2 else [word(896), word(960)]),
                            'Entangle captured placement/range differs')
                ranges = [row for row in part if row['event'] == 'target-range-query']
                require(ranges and all(row['range'] == word(500) and row['predict'] == 1 for row in ranges),
                        'Entangle configured range differs')
        else:
            require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows),
                    'Entangle control has observer hooks')
        streams.append(markers)
    require(streams[0] == streams[1] == streams[2], 'Entangle repeats/control differ')
    return dict(observations=2, controls=1, public_markers=561, engine_parity=False,
                remaining_owner='TARGET-01.2')


RANGE_BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-entangle-ranges-1.27.json.gz'
RANGE_SOURCES = ROOT / 'tools/ghidra/fixtures/research/entangle-ranges'
RANGE_SETTINGS = {
    'authored': (64, '155ed88a4bba94867816b516f5af0e77fa2b68d874c6d0d9e2599cb25114d652'),
    'zero': (0, '0a942ff610d9997b7155a509a81939f362d7ff950454ada0fad1fa65035b7bad'),
}


def audit_ranges(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(RANGE_BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False and set(data['settings']) == set(RANGE_SETTINGS), 'wrong Entangle range corpus')
    for setting, sample in data['settings'].items():
        distance, map_sha = RANGE_SETTINGS[setting]
        require(sample['range'] == distance and digest(literal(sample['map'], 'raw')) == map_sha,
                'Entangle range/map changed')
        require(len(sample['captures']) == 3, 'two range observations and control required')
        streams = []
        for index, capture in enumerate(sample['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            env = header['env']
            require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                    header['sha256'] == SHA and header['task'] == 'TARGET-01.2-entangle' and
                    header['prefix'] == 'TEN ', 'wrong owned range capture')
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                    'wrong range research environment')
            for name in NAMES:
                require(header['source_sha256'][name] == digest((RANGE_SOURCES / name).read_bytes()),
                        'Entangle range source changed ' + name)
            require(header['source_sha256']['map'] == map_sha, 'captured range map changed')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed range observer')
            markers = re.findall(r'call Preload\( "(TEN [^"\r\n]*)" \)', preload.decode())
            require(len(markers) == 267 and markers[-1] == 'TEN tick=5599 scene=4 label=complete',
                    'incomplete range public stream')
            public = [dict(token.split('=', 1) for token in value.split()[1:]) for value in markers]
            for scene in range(4):
                part = [row for row in public if row['scene'] == str(scene)]
                before = [row for row in part if row['label'] == 'before']
                issue = [row for row in part if row['label'] == 'entangle-return']
                end = [row for row in part if row['label'] == 'settled']
                require(len(before) == len(issue) == len(end) == 1 and before[0]['head'] == '0' and
                        issue[0]['accepted'] == '1' and issue[0]['head'] == end[0]['head'] == '0' and
                        end[0]['overlays'] == '0' and end[0]['minevisible'] == '1',
                        'range placement failure is not rejected admission')
            require(footer == dict(event='preload-file', sha256=digest(preload), markers=267, complete=True),
                    'incomplete range preload')
            if mode == 'observe':
                require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                        ['TEN tick=0 scene=-1 label=start'] + markers, 'range observer/preload differs')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'range observer never completed')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'range observer counts differ')
                placements = [row for row in rows if row['event'] == 'entangle-placement']
                require([row['scene'] for row in placements] == list(range(4)) and
                        all(row['result'] == 0 and row['range'] == word(distance) for row in placements),
                        'range constructor failure differs')
                require(not any(row['event'] in ('point-task', 'target-task') for row in rows),
                        'failed placement constructed tasks')
            else:
                require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows),
                        'range control has observer hooks')
            streams.append(markers)
        require(streams[0] == streams[1] == streams[2], 'range repeats/control differ')
    return dict(observations=4, controls=2, public_markers=1602, engine_parity=False,
                remaining_owner='TARGET-01.2')
