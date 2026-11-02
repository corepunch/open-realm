"""Literal ordinary-pickup producer witnesses; no complete trajectory claim."""
from collections import Counter
import gzip
import json
import re
import struct

from itemdrop_evidence import ROOT, SHA, digest, literal, require

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-itempickup-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/itempickup'
SETTINGS = {
    'stock': (150, '8c3ff97b291be7f6611a525e6629bb94fe694fb4d28f922499aa6b2261201861'),
    'authored': (64, 'ef16877ea6187f0349b845ef826286894abfad28ba13bc4c1807e427dfd0459a'),
    'zero': (0, '885557ffe720d89d6caabdb771fcf03fe5bfdefb0d303087f034fad183ba04b7'),
}
NAMES = ('itemdrop_capture.py', 'itemdrop_make_map.py', 'itemdrop_observer.js', 'itemdrop_probe.j')
CONTROL = {'metadata', 'owned-loading-input', 'owned-loading-input-skipped', 'loading-key',
           'control-start-file', 'preload-file'}


def public_contract(markers, distance):
    rows = [dict(word.split('=', 1) for word in value.split()[1:]) for value in markers]
    require(rows[-1]['label'] == 'complete' and rows[-1]['scene'] == '19' and
            rows[-1]['tick'] == '1899', 'incomplete pickup scenes')
    success = {0, 1, 2, 3, 4, 5, 9, 12, 15} if distance else {2, 3, 4, 15}
    for scene in range(19):
        part = [row for row in rows if row['scene'] == str(scene)]
        issue = [row for row in part if row['label'] == 'smart-return']
        end = [row for row in part if row['label'] == 'settled']
        require(len(issue) == len(end) == 1, 'missing pickup admission/completion')
        require(issue[0]['accepted'] == str(int(scene < 16)), 'pickup admission differs')
        head = '0' if scene in (5, 6, 18) else '851986' if scene == 16 else '851973' if scene == 17 else '851971'
        require(issue[0]['head'] == head, 'pickup public head differs')
        owned = scene in success or scene == 14
        removed = scene == 7 or (scene == 13 and distance != 0)
        require(end[0]['anyowned'] == str(int(owned)) and
                end[0]['itype'] == ('0' if removed else '1918989414'), 'pickup final item identity/ownership differs')
        if scene in success:
            require(end[0]['owned'] == '1918989414' and end[0]['visible'] == '0', 'pickup attachment differs')
        final_head = '851986' if scene == 12 and distance else '851973' if scene == 17 else '0'
        require(end[0]['head'] == final_head, 'pickup completion replaced a retained order')


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False and set(data['settings']) == set(SETTINGS), 'wrong pickup evidence contract')
    total = 0
    for setting, sample in data['settings'].items():
        distance, map_sha = SETTINGS[setting]
        require(sample['metadata']['pickup_range'] == distance and sample['metadata']['map_sha256'] == map_sha and
                digest(literal(sample['map'], 'raw')) == map_sha, 'pickup map/range changed')
        require(len(sample['captures']) == 3, 'two pickup observations and a control required')
        streams = []
        for index, capture in enumerate(sample['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                    header['sha256'] == SHA and header['task'] == 'TARGET-01.2-itempickup' and
                    header['prefix'] == 'TPK ', 'wrong owned pickup capture')
            env = header['env']
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env], 'wrong pickup research environment')
            for name in NAMES:
                require(header['source_sha256'][name] == digest((SOURCES / name).read_bytes()), 'pickup source changed ' + name)
            require(header['source_sha256']['map'] == map_sha, 'captured pickup map changed')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed pickup observer')
            markers = re.findall(r'call Preload\( "(TPK [^"\r\n]*)" \)', preload.decode())
            count = 293 if distance else 284
            require(len(markers) == count and footer == dict(event='preload-file', sha256=digest(preload),
                                                           markers=count, complete=True), 'incomplete pickup preload')
            public_contract(markers, distance)
            if mode == 'observe':
                require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                        ['TPK tick=0 scene=-1 label=start'] + markers, 'pickup observer/preload differs')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'missing pickup observer termination')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'pickup observer counts differ')
                tasks = [row for row in rows if row.get('event') == 'target-task']
                for scene in range(16):
                    pair = [row for row in tasks if row['scene'] == scene]
                    times = 2 if scene == 9 else 1
                    require([row['code'] for row in pair] == [0xd017e, 0xd0171] * times,
                            'pickup target task chain/resume differs')
                    word = struct.unpack('<I', struct.pack('<f', distance))[0]
                    require([row['range'] for row in pair] == [0, word] * times, 'pickup target task range differs')
                    require(all(row['target'] == pair[0]['target'] for row in pair), 'pickup task target identity differs')
                require(not any(row['scene'] >= 16 for row in tasks), 'rejected pickup constructed a task')
            else:
                require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows), 'pickup control has observer hooks')
            streams.append(markers)
            total += len(markers)
        require(streams[0] == streams[1] == streams[2], 'pickup repeats/control differ')
    return dict(observations=6, controls=3, public_markers=total, engine_parity=False, remaining_owner='TARGET-01.2')
