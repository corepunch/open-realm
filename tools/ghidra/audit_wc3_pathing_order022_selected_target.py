"""Retained target notifications across a selected recipient's RemoveUnit callback.

The literal streams include physical samples. Their trajectories are not this
contract: input delivery and autonomous reacquisition differ between runs.
"""
import base64
import gzip
import json
import re
from audit_wc3_pathing_order022_smart import ROOT, SHA, digest, literal, require, unwind

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-order022-selected-target-1.27.json.gz'
SOURCES = ['order022_selected_target_capture.py', 'order022_observer.js',
           'order022_selected_target_make_map.py', 'order022_selected_target_probe.j',
           'order022_smart_input.c', 'order022_smart_click.c']
PLAN = [dict(tick=t, pixel=[650, 300], alt=False, shift=False, key=k)
        for t, k in [(40, 'smart'), (120, 'attack')]]


def notification_contract(markers):
    """Compare only the authored event sequence; retain all sample bytes separately."""
    result = []
    for marker in markers:
        match = re.fullmatch(r'O022T tick=(\d+) scene=(-?\d+) age=(\d+) label=(.*)', marker)
        require(match is not None, 'malformed authored marker')
        label = match[4]
        if label.startswith(('sample ', 'terminal ')):
            continue
        result.append((int(match[2]), label))
    return result


def expected_contract():
    result = [(-1, 'ticking c=0 p=0')]
    for scene, head in enumerate((851971, 851983)):
        result += [(scene, 'created c=0 p=0'),
                   (scene, f'issued-target unit=1 id={head} target=2 c={head} p=0'),
                   (scene, f'target-replaced c={head} p=0'),
                   (scene, f'issued-target unit=3 id={head} target=2 c={head} p={head}')]
    return result + [(2, 'complete c=0 p=0')]


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['task'] == 'ORDER-02.2' and data['version'] == 1, 'wrong contract identity')
    require(len(data['captures']) == 3, 'two observed repeats and unhooked control required')
    artifacts = {}
    for name in ('map', 'helper_exe', 'helper_so'):
        item = data[name]
        require(digest(base64.b64decode(item['raw_b64'], validate=True)) == item['sha256'],
                'map/helper artifact changed')
        artifacts[name] = item['sha256']
    meta = data['map_metadata']
    require(meta['map_sha256'] == artifacts['map'] and meta['variant'] == 'selected-target', 'wrong authored map')
    require(meta['probe_template_sha256'] ==
            digest((ROOT / 'tools/frida/research/order022_selected_target_probe.j').read_bytes()),
            'authored selected-target probe changed')
    public = []
    for index, capture in enumerate(data['captures']):
        rows = [json.loads(line) for line in literal(capture).splitlines()]
        preload = literal(capture, 'preload')
        header, footer = rows[0], rows[-1]
        mode = 'control' if index == 2 else 'observe'
        require(header['event'] == 'metadata' and header['mode'] == mode and header['sha256'] == SHA and
                header['owned'] and header['task'] == 'ORDER-02.2' and header['prefix'] == 'O022T ',
                'wrong owned native capture')
        require(header['env'] in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[header['env']] and
                header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[header['env']],
                'wrong research environment')
        pins = header['source_sha256']
        for name in SOURCES:
            require(pins[name] == digest((ROOT / 'tools/frida/research' / name).read_bytes()), 'changed source ' + name)
        require(pins['map'] == artifacts['map'] and pins['order022-smart.exe'] == artifacts['helper_exe'] and
                pins['order022-smart.exe.so'] == artifacts['helper_so'], 'captured map/helper differs')
        require(header['input'] == PLAN, 'selected target input plan changed')
        inputs = [row for row in rows if row.get('event') == 'player-input']
        require(len(inputs) == 2 and [row['plan'] for row in inputs] == PLAN and
                all(row['rc'] == 0 and row['cmd'] == [str(header['pid']), '650', '300', plan['key']]
                    for row, plan in zip(inputs, PLAN)), 'missing real owned target input')
        require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                'failed capture')
        markers = re.findall(r'call Preload\( "(O022T [^"\r\n]*)" \)', preload.decode())
        require(len(markers) == 28 and footer == dict(event='preload-file', sha256=digest(preload),
                                                    markers=28, complete=True), 'incomplete literal preload')
        require(markers[-1] == 'O022T tick=180 scene=2 age=80 label=complete c=0 p=0', 'probe never finished')
        contract = notification_contract(markers)
        require(contract == expected_contract(), 'original-target notification sequence differs')
        if mode == 'observe':
            unwind(rows)
            require([row['value'] for row in rows if row.get('event') == 'marker' and
                     'label=start' not in row['value']] == markers, 'observer differs from its literal preload')
            packets = [row for row in rows if row.get('event') == 'net-order']
            events = [row for row in rows if row.get('event') == 'target-event']
            require(len(packets) == len(events) == 4, 'missing selected recipient packet or target event')
            for head in (851971, 851983):
                pair = [row for row in packets if row['order']['command'] == head]
                notifications = [row for row in events if row['order']['command'] == head]
                require(len(pair) == len(notifications) == 2 and len({row['unit'] for row in pair}) == 2,
                        'selected pair missing or duplicated')
                target = pair[0]['order']['target']
                point = pair[0]['order']['point']
                require(target != [0xffffffff, 0xffffffff] and
                        all(row['order']['target'] == target and row['order']['point'] == point
                            for row in pair + notifications), 'canonical target or point changed between recipients')
                require(all(row['caller'] == 0x6ba4c9 and row['flags'] == 8 and row['fallback'] == 0
                            for row in pair), 'selected producer or admission mode changed')
                require([row['unit'] for row in notifications] == [row['unit'] for row in pair] and
                        all(row['state']['head_order']['command'] == head for row in notifications),
                        'native target-event owner differs')
        else:
            require(not any('seq' in row or row.get('event') in ('module', 'trace-end', 'net-order') for row in rows),
                    'control has observer hooks')
        public.append(contract)
    require(public[0] == public[1] == public[2], 'repeat/control notification contract differs')
    return dict(captures=2, controls=1, scenes=6, public_markers=84, notification_contract_markers=30,
                target_events=12, engine_parity=False, trajectory_parity=False, remaining_owner='ORDER-02.2')
