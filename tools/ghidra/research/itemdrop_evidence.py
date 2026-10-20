"""Literal V2 item-drop observations and controls; this is not engine acceptance."""
import base64
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[3]
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-itemdrop-point-1.27.json.gz'
SOURCES = ROOT / 'tools/ghidra/fixtures/research/itemdrop'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def literal(item, key):
    raw = base64.b64decode(item[key + '_b64'], validate=True)
    require(digest(raw) == item[key + '_sha256'], 'literal capture bytes changed')
    return raw


def fields(marker):
    require(marker.startswith('TDR '), 'wrong probe marker')
    return dict(part.split('=', 1) for part in marker.split()[1:])


def public_contract(markers, distance):
    parsed = [fields(value) for value in markers]
    require(parsed[-1]['label'] == 'complete' and parsed[-1]['tick'] == '599' and
            parsed[-1]['scene'] == '12', 'probe never finished')
    for scene in range(12):
        part = [row for row in parsed if row['scene'] == str(scene)]
        issued = [row for row in part if row['label'] == 'drop-return']
        settled = [row for row in part if row['label'] == 'settled']
        require(len(issued) == len(settled) == 1, 'missing public scene boundaries')
        admitted = scene not in (8, 9)
        require(issued[0]['accepted'] == str(int(admitted)) and
                issued[0]['head'] == ('852001' if admitted else '0'), 'public drop admission/head differs')
        require(settled[0]['head'] == '0', 'drop owner never retired')
        owned = scene in (5, 6, 8, 9) or (scene == 4 and distance == 64)
        require(settled[0]['owned'] == str(int(owned)), 'settled item ownership differs')
        if scene == 6:
            stopped = [row for row in part if row['label'] == 'stop-return']
            require(len(stopped) == 1 and stopped[0]['head'] == '0' and stopped[0]['owned'] == '1',
                    'Stop released the pending carried item')
        if scene in (10, 11):
            label = 'remove-return' if scene == 10 else 'unequip-return'
            removed = [row for row in part if row['label'] == label]
            require(len(removed) == 1 and removed[0]['head'] == '852001' and
                    removed[0]['owned'] == removed[0]['carried'] == '0', 'item removal retired the native user head')
            require(float(settled[0]['x']) > 1000 and float(settled[0]['x']) > float(removed[0]['x']) + 150,
                    'item removal aborted the retained point journey')
    return parsed


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and
            set(data['settings']) == {'stock', 'authored'}, 'wrong item-drop contract')
    total = 0
    for kind, setting in data['settings'].items():
        distance = 100 if kind == 'stock' else 64
        require(setting['range'] == distance and setting['map_metadata']['drop_range'] == distance,
                'authored drop range changed')
        artifact = setting['map']
        require(digest(base64.b64decode(artifact['raw_b64'], validate=True)) == artifact['sha256'] ==
                setting['map_metadata']['map_sha256'], 'authored map artifact changed')
        require(len(setting['captures']) == 3, 'two repeats and an unhooked control required per setting')
        public = []
        for index, capture in enumerate(setting['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            require(header['event'] == 'metadata' and header['mode'] == mode and header['sha256'] == SHA and
                    header['owned'] and header['task'] == 'TARGET-01.2-itemdrop' and header['prefix'] == 'TDR ',
                    'wrong owned native capture')
            env = header['env']
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                    'wrong research environment')
            pins = header['source_sha256']
            for name in ['itemdrop_capture.py', 'itemdrop_make_map.py', 'itemdrop_observer.js', 'itemdrop_probe.j']:
                require(pins[name] == digest((SOURCES / name).read_bytes()), 'changed native source ' + name)
            require(pins['map'] == artifact['sha256'], 'captured map differs')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed native capture')
            markers = re.findall(r'call Preload\( "(TDR [^"\r\n]*)" \)', preload.decode())
            require(len(markers) == 617 and footer == dict(event='preload-file', sha256=digest(preload),
                                                         markers=617, complete=True), 'incomplete literal preload')
            public_contract(markers, distance)
            if mode == 'observe':
                require([row['value'] for row in rows if row.get('event') == 'marker'] == markers,
                        'observer differs from its literal preload')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'missing observer termination')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'observer counts differ')
                returns = [row for row in rows if row.get('event') == 'public-drop-point-return']
                require(len(returns) == 12 and all(row['result'] == int(row['scene'] not in (8, 9))
                                                 for row in returns), 'native return contract differs')
                tasks = [row for row in rows if row.get('event') == 'point-task']
                requests = [row for row in rows if row.get('event') == 'point-request']
                bits = struct.unpack('<I', struct.pack('<f', distance))[0]
                require(len(tasks) == len(requests) == 10 and
                        all(row['code'] == 0xd016b and row['range'] == bits and row['association'] == '0x0'
                            for row in tasks) and all(row['range'] == bits for row in requests),
                        'native point-task/range contract differs')
                require(not any(row.get('event') == 'target-task' for row in rows), 'point drop became a target task')
            else:
                allowed = {'metadata', 'owned-loading-input', 'owned-loading-input-skipped',
                           'loading-key', 'control-start-file', 'preload-file'}
                require(all(row.get('event') in allowed and 'seq' not in row for row in rows),
                        'control contains observer hooks')
            public.append(markers)
            total += len(markers)
        require(public[0] == public[1] == public[2], 'repeat/control public markers differ')
    return dict(captures=4, controls=2, scenes=72, public_markers=total,
                engine_parity=False, remaining_owner='TARGET-01.2')


ADMISSION_BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-itemdrop-admission-1.27.json.gz'
ADMISSION_SOURCES = SOURCES.parent / 'itemdrop_admission'
IMMOBILE_BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-itemdrop-immobile-1.27.json.gz'
IMMOBILE_SOURCES = SOURCES.parent / 'itemdrop_immobile'
ADMISSION_GUARDS = {'0x206b90': '5b3c808375f384d7fa1807d1ebd4bb660e66286c5ba5b27dafc7de4912dc71d2', '0x2188b0': '84f6c87858f57563f844592bc035ebdd206f0e3574bbf22d2ac2e1345c1f61b9', '0x55b420': '2307c83ffcbd84958cb4f430777b22a9ee57f6fe7edd59eadac43c9edcada75f', '0x67abe0': '730f6e98493fcd8117bfd7c840da40f26528e5da727606a0e78618504b79379d', '0x684ab0': '080c753281b06e755850f493adfa998850f2bfb7179a1cfa83ad99b65e150e1c', '0x6868f0': '53d933ffdd5654b8e824e109c97ff67d231163aa8ca3887753550ef1d9e1908d', '0x689960': '222b940d4bde93a7f74b23b13ee45da7ae3a1ccabb643e912bc69a2ad00a6a8e', '0x69bc80': 'b99ff619585ac58592f21591240f88d3ece75be127cb25625a46aa1ddadab59f'}


def admission_fields(marker):
    require(marker.startswith('TDA '), 'wrong admission probe marker')
    return dict(part.split('=', 1) for part in marker.split()[1:])


def admission_contract(markers, immobile=False):
    parsed = [admission_fields(value) for value in markers]
    require(len(parsed) == 273 and parsed[-1]['label'] == 'complete' and
            parsed[-1]['tick'] == '239' and parsed[-1]['scene'] == '12', 'incomplete admission probe')
    events = [row for row in parsed if row['label'] == 'event']
    require([(row['kind'], row['order']) for row in events] ==
            [('point', '851986'), ('target', '851986'), ('immediate', '851972')],
            'issued event kind/positive controls differ')
    require(events[0]['ox'] == '512.000' and events[0]['oy'] == '1056.000' and
            events[1]['targetunit'] == '1751543663', 'positive event context differs')
    for kind in ('point', 'target', 'immediate'):
        control, = [r for r in parsed if r['label'] == 'control-' + kind]
        require(control['accepted'] == control['events'] == '1', 'positive control was not accepted/notified')
    for scene in range(12):
        part = [row for row in parsed if row['scene'] == str(scene)]
        before, = [row for row in part if row['label'] == 'before']
        returned, = [row for row in part if row['label'] == 'drop-return']
        settled, = [row for row in part if row['label'] == 'settled']
        accepted = scene not in ((2, 3, 8, 10) if immobile else (2, 3, 10))
        require(returned['accepted'] == str(int(accepted)) and returned['events'] == '0',
                'drop admission or silent issued frontier differs')
        want_head = '852001' if accepted else ('851973' if scene == 10 else '0')
        if immobile and scene in (6, 7):
            want_head = '0'
            require(returned['type'] == '1751674741' and returned['x'] == '704.000' and
                    returned['y'] == '1024.000' and returned['owned'] == str(int(scene == 7)) and
                    settled['x'] == returned['x'] and settled['y'] == returned['y'] and
                    settled['owned'] == returned['owned'], 'immobile synchronous point completion differs')
        require(returned['head'] == want_head, 'drop current head differs')
        if scene == 5:
            require(before['head'] == '851986' and returned['head'] == '852001', 'active Move was not replaced')
        if scene == 10:
            require(before['head'] == returned['head'] == '851973' and returned['owned'] == '1' and
                    settled['x'] == '704.000' and settled['y'] == '1056.000' and settled['owned'] == '1',
                    'paused rejection changed suspension or item')
    require(not any(row['label'].startswith('nested-') for row in parsed), 'silent drop fired reentrant issued code')


def audit_admission(bundle=None, immobile=False):
    path = IMMOBILE_BUNDLE if immobile else ADMISSION_BUNDLE
    sources = IMMOBILE_SOURCES if immobile else ADMISSION_SOURCES
    data = bundle if bundle is not None else json.loads(gzip.decompress(path.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA,
            'wrong admission contract')
    require(set(data['instruction_guards']) == set(ADMISSION_GUARDS) and
            all(digest(bytes.fromhex(raw)) == ADMISSION_GUARDS[address]
                for address, raw in data['instruction_guards'].items()), 'original admission code changed')
    for name, source in data['sources'].items():
        raw = base64.b64decode(source['raw_b64'], validate=True)
        require(digest(raw) == source['sha256'] == digest((sources / name).read_bytes()),
                'admission source provenance changed')
    require(set(data['sources']) == {'itemdrop_capture.py', 'itemdrop_observer.js',
                                    'itemdrop_make_map.py', 'itemdrop_probe.j'}, 'missing admission source')
    artifact = data['map']
    require(digest(base64.b64decode(artifact['raw_b64'], validate=True)) == artifact['sha256'] ==
            artifact['metadata']['map_sha256'], 'admission map bytes differ')
    for name, pin in artifact['metadata']['sources'].items():
        require(pin == data['sources'][name]['sha256'], 'map builder/probe provenance changed')
    decomp = data['decompilation']
    require(digest(base64.b64decode(decomp['raw_b64'], validate=True)) == decomp['sha256'],
            'admission decompilation bytes differ')
    require(len(data['captures']) == 3, 'two observations and an unhooked control required')
    public = []
    for index, capture in enumerate(data['captures']):
        rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
        preload = literal(capture, 'preload')
        header, footer = rows[0], rows[-1]
        mode = 'observe' if index < 2 else 'control'
        require(header['event'] == 'metadata' and header['mode'] == mode and header['sha256'] == SHA and
                header['owned'] and header['prefix'] == 'TDA ', 'unowned/wrong admission capture')
        env = header['env']
        require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                'admission research environment differs')
        pins = header['source_sha256']
        require(pins['map'] == artifact['sha256'] and
                all(pins[name] == source['sha256'] for name, source in data['sources'].items()),
                'admission captured source pins changed')
        require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                'failed admission capture')
        markers = re.findall(r'call Preload\( "(TDA [^"\r\n]*)" \)', preload.decode())
        admission_contract(markers, immobile)
        require(footer == dict(event='preload-file', sha256=digest(preload), markers=273, complete=True),
                'incomplete admission preload')
        if mode == 'observe':
            require([row['value'] for row in rows if row.get('event') == 'marker'] == markers,
                    'admission observer differs from literal preload')
            ends = [row for row in rows if row.get('event') == 'trace-end']
            require(len(ends) == 1 and ends[0]['installed'], 'missing admission observer termination')
            counts = ends[0]['counts']
            require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                    'admission observer counts changed')
            returns = [r for r in rows if r.get('event') == 'public-drop-point-return']
            require(len(returns) == 12 and all(r['result'] == int(r['scene'] not in ((2, 3, 8, 10) if immobile else (2, 3, 10))) for r in returns),
                    'original admission returns changed')
            producers = [r for r in rows if r.get('event') == 'drop-producer-enter']
            tasks = [r for r in rows if r.get('event') == 'point-task' and r['caller'] == 5567059]
            require(len(producers) == len(tasks) == (8 if immobile else 9) and
                    all(r['code'] == 852331 and r['range'] == 1120403456 and r['association'] == '0x0'
                        for r in tasks), 'specialized admission physical task differs')
        else:
            allowed = {'metadata', 'owned-loading-input', 'owned-loading-input-skipped',
                       'loading-key', 'control-start-file', 'preload-file'}
            require(all(row.get('event') in allowed and 'seq' not in row for row in rows),
                    'admission control contains observer hooks')
        public.append(markers)
    require(public[0] == public[1] == public[2], 'admission repeat/control markers differ')
    return dict(observations=2, controls=1, public_markers=819, positive_event_kinds=3,
                drop_issued_events=0, paused_drop_accepted=False, engine_parity=False)
