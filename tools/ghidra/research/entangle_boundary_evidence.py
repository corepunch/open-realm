"""Adjacent original Aent ranges: public admission and retained placement."""
from collections import Counter
import gzip
import json
import re

from entangle_producer_evidence import CONTROL, NAMES, ROOT, SHA, digest, literal, require, word

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-entangle-boundaries-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/entangle-boundaries'
SETTINGS = {
    'below': (317.75, 'cf8a918982a830f77a8d0160e3b156318826beb366a4f29a3c04352b94a0e55a'),
    'equal': (318, '65b822fea44209903baa1ea56415397a092b1027e06e560f19f797d436b73ad7'),
    'above': (318.25, 'da490ca327d9bf2d37174d7c1f42208cbaca86530694fd89f77a38fd3e4ebc19'),
}


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False and set(data['settings']) == set(SETTINGS), 'wrong boundary corpus')
    for setting, sample in data['settings'].items():
        distance, map_sha = SETTINGS[setting]
        require(sample['range'] == distance and digest(literal(sample['map'], 'raw')) == map_sha,
                'boundary range/map changed')
        require(len(sample['captures']) == 3, 'two boundary observations and control required')
        streams = []
        for index, capture in enumerate(sample['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            env = header['env']
            require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                    header['sha256'] == SHA and header['task'] == 'TARGET-01.2-entangle' and
                    header['prefix'] == 'TEN ', 'wrong owned boundary capture')
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                    'wrong boundary research environment')
            for name in NAMES:
                require(header['source_sha256'][name] == digest((SOURCES / name).read_bytes()), 'boundary source changed')
            require(header['source_sha256']['map'] == map_sha, 'captured boundary map changed')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed boundary observer')
            markers = re.findall(r'call Preload\( "(TEN [^"\r\n]*)" \)', preload.decode())
            require(len(markers) == 94 and markers[-1] == 'TEN tick=1999 scene=2 label=complete',
                    'incomplete boundary public stream')
            public = [dict(token.split('=', 1) for token in value.split()[1:]) for value in markers]
            for scene in range(2):
                part = [row for row in public if row['scene'] == str(scene)]
                before = [row for row in part if row['label'] == 'before']
                issue = [row for row in part if row['label'] == 'entangle-return']
                end = [row for row in part if row['label'] == 'settled']
                direct = setting != 'below' and scene == 0
                require(len(before) == len(issue) == len(end) == 1 and before[0]['head'] == '0' and
                        issue[0]['accepted'] == '1' and issue[0]['head'] == ('0' if direct else '852147'),
                        'boundary direct/retained admission differs')
                require(issue[0]['overlays'] == str(int(direct)) and end[0]['head'] == '0' and
                        end[0]['overlays'] == '1' and end[0]['minevisible'] == '0', 'boundary terminal overlay differs')
                site = (768, 960) if setting == 'below' else (704, 1024)
                require((end[0]['x'], end[0]['y']) == tuple(f'{v:.3f}' for v in site), 'boundary root site differs')
            require(footer == dict(event='preload-file', sha256=digest(preload), markers=94, complete=True),
                    'incomplete boundary preload')
            if mode == 'observe':
                require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                        ['TEN tick=0 scene=-1 label=start'] + markers, 'boundary observer/preload differs')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'boundary observer never completed')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'boundary observer counts differ')
                ranges = [row for row in rows if row['event'] == 'target-range-query']
                require(ranges and all(row['range'] == word(distance) for row in ranges), 'boundary authored range differs')
                for scene in range(2):
                    part = [row for row in rows if row.get('scene') == scene]
                    direct = setting != 'below' and scene == 0
                    placements = [row for row in part if row['event'] == 'entangle-placement']
                    site = (768, 960) if setting == 'below' else (704, 1024)
                    require(len(placements) == (0 if direct else 1) and all(row['result'] == 1 and
                            row['range'] == word(distance) and row['point'] == [word(v) for v in site] and
                            row['approach'] == int(setting == 'below') for row in placements), 'boundary constructor differs')
                    points = [row for row in part if row['event'] == 'point-task']
                    require([row['code'] for row in points] == ([] if direct else
                            [0xd016b, 0xd018f] if setting == 'below' else [0xd018f]) and
                            all(row['range'] == 0 and row['point'] == [word(v) for v in site] for row in points),
                            'boundary physical task differs')
            else:
                require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows),
                        'boundary control has observer hooks')
            streams.append(markers)
        require(streams[0] == streams[1] == streams[2], 'boundary repeats/control differ')
    return dict(observations=6, controls=3, public_markers=846, engine_parity=False,
                remaining_owner='TARGET-01.2')
