"""Native admission-owned occupant Move and bounded Root placement45 retries."""
from collections import Counter
import gzip
import json
import re

from entangle_producer_evidence import CONTROL, NAMES, ROOT, SHA, digest, literal, require, word

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-entangle-retry-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/entangle-retry'
MAP_SHA = '2381e9d3fe4382223aff8c4c8928c8153eb619bc9f8ebdd41d9ea461a5f2610b'


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False, 'wrong Root retry corpus')
    require(digest(literal(data['map'], 'raw')) == MAP_SHA, 'retry map changed')
    require(len(data['captures']) == 3, 'two retry observations and control required')
    streams = []
    for index, capture in enumerate(data['captures']):
        rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
        preload = literal(capture, 'preload')
        header, footer = rows[0], rows[-1]
        mode = 'control' if index == 2 else 'observe'
        env = header['env']
        require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                header['sha256'] == SHA and header['task'] == 'TARGET-01.2-entangle' and
                header['prefix'] == 'TEN ', 'wrong owned retry capture')
        require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                'wrong retry research environment')
        for name in NAMES:
            require(header['source_sha256'][name] == digest((SOURCES / name).read_bytes()), 'retry source changed')
        require(header['source_sha256']['map'] == MAP_SHA, 'captured retry map changed')
        require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                'failed retry observer')
        markers = re.findall(r'call Preload\( "(TEN [^"\r\n]*)" \)', preload.decode())
        require(len(markers) == 93 and markers[-1] == 'TEN tick=1999 scene=6 label=complete',
                'incomplete retry public stream')
        public = [dict(token.split('=', 1) for token in value.split()[1:]) for value in markers]
        for scene in range(2):
            part = [row for row in public if row['scene'] == str(scene)]
            issue = [row for row in part if row['label'] == 'entangle-return']
            end = [row for row in part if row['label'] == 'settled']
            require(len(issue) == len(end) == 1 and issue[0]['head'] == '852147' and
                    issue[0]['whead'] == '851986', 'occupant Move must precede Unroot')
            require(end[0]['head'] == '0' and end[0]['overlays'] == str(1-scene) and
                    end[0]['minevisible'] == str(scene), 'slow occupant Root failure differs')
            if scene == 1:
                require(end[0]['whead'] == '851986' and
                        (end[0]['x'], end[0]['y']) == ('1009.443', '1026.825'), 'failed Root consumed occupant Move')
        require(footer == dict(event='preload-file', sha256=digest(preload), markers=93, complete=True),
                'incomplete retry preload')
        if mode == 'observe':
            require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                    ['TEN tick=0 scene=-1 label=start'] + markers, 'retry observer/preload differs')
            ends = [row for row in rows if row.get('event') == 'trace-end']
            require(len(ends) == 1 and ends[0]['installed'], 'retry observer never completed')
            counts = ends[0]['counts']
            require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                    'retry observer counts differ')
            slow = [row for row in rows if row.get('scene') == 1]
            begin = [row for row in slow if row['event'] == 'root-retry-enter']
            end = [row for row in slow if row['event'] == 'root-retry-leave']
            require([row['retries'] for row in begin] == list(range(11)) and
                    [row['retries'] for row in end] == list(range(1, 11))+[0], 'bounded retry counter differs')
            require(sum(row['event'] == 'root-defer-enter' for row in slow) == 10 and
                    sum(row['event'] == 'root-resume-enter' for row in slow) == 10, 'deferred retry ownership differs')
            build = [row for row in slow if row['event'] == 'entangle-build-filter' and
                     0x438ac0 <= row['caller'] < 0x438d00]
            require(len(build) == 11 and all(row['result'] == 0x45 and
                    row['constants'] == [word(32), word(128)] for row in build), 'retry placement45 differs')
            clocks = [row['clock']['primary'][0] for row in begin]
            import struct
            seconds = [struct.unpack('<f', struct.pack('<I', value))[0] for value in clocks]
            require(all(.49 < b-a < .51 for a, b in zip(seconds, seconds[1:])), 'half-second retry delay differs')
        else:
            require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows),
                    'retry control has observer hooks')
        streams.append(markers)
    require(streams[0] == streams[1] == streams[2], 'retry repeats/control differ')
    return dict(observations=2, controls=1, public_markers=279, retries=10, engine_parity=False,
                remaining_owner='TARGET-01.2')
