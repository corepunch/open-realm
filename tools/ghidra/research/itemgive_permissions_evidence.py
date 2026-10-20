"""Literal V7 Inventory permission witnesses; broader producer parity stays open."""
from collections import Counter
import gzip
import json
import re

from itemdrop_evidence import ROOT, SHA, digest, literal, require

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-itemgive-permissions-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/itemgive_permissions'
MAPS = {'can-get': '3c51d74259f244dec19d44f336b8267b237a91ed77006fa3bbd5793d0c9b287a',
        'no-get': 'db453510d56068b824cec1c78da4494930f619f53a6a5c67fb3a979840b42bb1'}
SOURCE_NAMES = ('itemdrop_capture.py', 'itemdrop_make_map.py', 'itemdrop_observer.js', 'itemdrop_probe.j')
CONTROL_EVENTS = {'metadata', 'owned-loading-input', 'owned-loading-input-skipped',
                  'loading-key', 'control-start-file', 'preload-file'}


def public_contract(markers, flag):
    rows = [dict(part.split('=', 1) for part in line.split()[1:]) for line in markers]
    require(rows[-1]['label'] == 'complete' and rows[-1]['tick'] == '599' and
            rows[-1]['scene'] == '3', 'incomplete permission probe')
    for scene, label in enumerate(('give-return', 'script-return', 'smart-return')):
        part = [row for row in rows if row['scene'] == str(scene)]
        caps = [row for row in part if row['label'] == 'capacities']
        issued = [row for row in part if row['label'] == label]
        settled = [row for row in part if row['label'] == 'settled']
        require(len(caps) == len(issued) == len(settled) == 1, 'missing permission producer')
        require(caps[0]['source'] == caps[0]['target'] == '6', 'capacity changed with permission')
        accepted = scene != 2 or flag
        require(issued[0]['accepted'] == str(int(accepted)), 'producer permission admission differs')
        if scene < 2:
            require(issued[0]['received'] == '1918989414' and issued[0]['owned'] == '0' and
                    issued[0]['anyowned'] == '1' and issued[0]['visible'] == '0', 'forced receipt is not synchronous')
        require(settled[0]['head'] == '0' and settled[0]['owned'] == '0' and
                settled[0]['received'] == ('1918989414' if accepted else '0') and
                settled[0]['anyowned'] == str(int(accepted)) and
                settled[0]['visible'] == str(int(not accepted)), 'settled permission ownership differs')


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and
            set(data['settings']) == set(MAPS), 'wrong permission evidence contract')
    total = 0
    for variant, setting in data['settings'].items():
        flag = int(variant == 'can-get')
        metadata = setting['metadata']
        require(metadata['can_get_items'] == flag and metadata['give_range'] == 150 and
                metadata['map_sha256'] == MAPS[variant] == digest(literal(setting['map'], 'raw')),
                'authored permission map changed')
        require(len(setting['captures']) == 3, 'two observations and an unhooked control required')
        streams = []
        for index, capture in enumerate(setting['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                    header['sha256'] == SHA and header['task'] == 'TARGET-01.2-itemgive' and
                    header['prefix'] == 'TGV ', 'wrong owned permission capture')
            env = header['env']
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                    'wrong permission research environment')
            for name in SOURCE_NAMES:
                require(header['source_sha256'][name] == digest((SOURCES / name).read_bytes()),
                        'permission probe source changed: ' + name)
            require(header['source_sha256']['map'] == MAPS[variant], 'captured permission map changed')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed permission observation')
            markers = re.findall(r'call Preload\( "(TGV [^"\r\n]*)" \)', preload.decode())
            count = 80 if flag else 78
            require(len(markers) == count and footer == dict(event='preload-file', sha256=digest(preload),
                                                           markers=count, complete=True), 'incomplete permission preload')
            public_contract(markers, flag)
            if mode == 'observe':
                observed = [row['value'] for row in rows if row.get('event') == 'marker']
                require(observed == ['TGV tick=0 scene=-1 label=start'] + markers,
                        'permission observer/preload markers differ')
                getters = [row for row in rows if row.get('event') == 'can-get-items']
                require(len(getters) == (3 if flag else 1) and
                        all(row['scene'] == 2 and row['value'] == flag for row in getters),
                        'native pickup permission getter differs')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'missing permission observer termination')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'permission observer counts differ')
            else:
                require(all(row.get('event') in CONTROL_EVENTS and 'seq' not in row for row in rows),
                        'permission control contains observer hooks')
            streams.append(markers)
            total += len(markers)
        require(streams[0] == streams[1] == streams[2], 'permission repeat/control markers differ')
    return dict(observations=4, controls=2, public_markers=total, engine_parity=False,
                remaining_owner='TARGET-01.2')
