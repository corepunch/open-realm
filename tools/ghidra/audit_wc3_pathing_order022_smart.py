"""Audit literal direct-Smart captures, including real right-click and unhooked control."""
import base64
import gzip
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_pathing_order022 import ROOT, SHA, unwind

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-order022-smart-head-1.27.json.gz'
SOURCES = ['order022_smart_capture.py', 'order022_observer.js', 'order022_smart_make_map.py',
           'order022_smart_probe.j', 'order022_smart_input.c', 'order022_smart_click.c']
PLAN = [dict(tick=t, pixel=[650, 300], alt=False, shift=False, key='smart') for t in (200, 280)]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def literal(item, key='raw'):
    raw = base64.b64decode(item[key + '_b64'], validate=True)
    require(digest(raw) == item[key + '_sha256'], 'literal capture bytes changed')
    return raw


def sections(rows):
    result = {i: [] for i in range(5)}
    scene = -1
    for row in rows:
        if row.get('event') == 'marker':
            match = re.search(r'scene=(-?\d+) ', row['value'])
            if match:
                scene = int(match[1])
        if scene in result:
            result[scene].append(row)
    return result


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(len(data['captures']) == 3, 'two observed repeats and unhooked control required')
    artifacts = {}
    for name in ('map', 'helper_exe', 'helper_so'):
        item = data[name]
        raw = base64.b64decode(item['raw_b64'], validate=True)
        require(digest(raw) == item['sha256'], 'map/helper artifact changed')
        artifacts[name] = item['sha256']
    meta = data['map_metadata']
    require(meta['map_sha256'] == artifacts['map'] and meta['variant'] == 'smart-head', 'wrong authored map')
    require(meta['probe_template_sha256'] == digest((ROOT / 'tools/frida/research/order022_smart_probe.j').read_bytes()),
            'authored Smart probe changed')
    public = []
    for index, capture in enumerate(data['captures']):
        rows = [json.loads(line) for line in literal(capture).splitlines()]
        preload = literal(capture, 'preload')
        header, footer = rows[0], rows[-1]
        expected = 'control' if index == 2 else 'observe'
        require(header['event'] == 'metadata' and header['mode'] == expected and header['sha256'] == SHA and
                header['owned'] and header['task'] == 'ORDER-02.2' and header['prefix'] == 'O022S ', 'wrong owned native capture')
        require(header['env'] in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[header['env']] and
                header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[header['env']], 'wrong research environment')
        pins = header['source_sha256']
        for name in SOURCES:
            require(pins[name] == digest((ROOT / 'tools/frida/research' / name).read_bytes()), 'changed source ' + name)
        require(pins['map'] == artifacts['map'] and pins['order022-smart.exe'] == artifacts['helper_exe'] and
                pins['order022-smart.exe.so'] == artifacts['helper_so'], 'captured map/helper differs')
        require(header['input'] == PLAN, 'right-click plan changed')
        inputs = [row for row in rows if row.get('event') == 'player-input']
        require(len(inputs) == 2 and [row['plan'] for row in inputs] == PLAN and
                all(row['rc'] == 0 and row['cmd'][-1] == 'smart' for row in inputs), 'missing real owned right-click')
        require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows), 'failed capture')
        markers = re.findall(r'call Preload\( "(O022S [^"\r\n]*)" \)', preload.decode())
        require(len(markers) == 72 and footer == dict(event='preload-file', sha256=digest(preload), markers=72, complete=True),
                'incomplete literal preload')
        require(markers[-1] == 'O022S tick=520 scene=5 age=80 label=complete head=-1', 'probe never finished')
        parsed = []
        for marker in markers:
            match = re.fullmatch(r'O022S tick=(\d+) scene=(-?\d+) age=(\d+) label=(.*)', marker)
            require(match is not None, 'bad authored marker')
            parsed.append((int(match[2]), int(match[3]), match[4]))
        for scene in range(5):
            values = [(age, value) for s, age, value in parsed if s == scene]
            expected_head = 851983 if scene == 1 else 851971
            issued = [(age, value) for age, value in values if value.startswith('issued ')]
            require(len(issued) == 1 and issued[0][1] == f'issued event={expected_head} head={expected_head} target=2',
                    'direct Smart/Attack target notification or head differs')
            require(values[0] == (0, 'created head=0') and values[-1][1] == f'terminal head={expected_head}',
                    'missing idle start or persistent public head')
            if scene == 3:
                require(issued[0][0] > 140 and (10, 'cast accepted=1 head=852600') in values, 'UI Smart interrupted windup')
            if scene == 4:
                require((10, 'cast accepted=1 head=852600') in values and
                        (20, 'jass-smart-cast accepted=1 head=851971') in values and issued[0][0] == 20,
                        'script Smart did not replace windup synchronously')
        if expected == 'observe':
            unwind(rows)
            require([row['value'] for row in rows if row.get('event') == 'marker' and 'label=start' not in row['value']] == markers,
                    'observer differs from its literal preload')
            parts = sections(rows)
            for scene in range(5):
                orders = [row for row in parts[scene] if row['event'] == 'target-event' and
                          row['order']['command'] in (851971, 851983)]
                head = 851983 if scene == 1 else 851971
                require(len(orders) == 1 and orders[0]['order']['command'] == head and
                        orders[0]['state']['head_order']['command'] == head, 'native target-event owner differs')
            packets = [row for row in rows if row.get('event') == 'net-order']
            require(len(packets) == 2 and [row['fallback'] for row in packets] == [0, 1] and
                    all(row['order']['command'] == 851971 and not row['flags'] & 1 for row in packets),
                    'right-click target packet/head protection differs')
            protected = packets[1]
            require(protected['state']['head_order']['command'] == 852600 and
                    protected['state']['head_order']['flags'] & 0x40, 'missing protected original spell head')
            after = next(row for row in rows[rows.index(protected) + 1:] if row['event'] == 'net-order-end')
            require(after['state']['count'] == 2 and after['state']['head_order']['command'] == 852600 and
                    after['state']['tail_order']['command'] == 851971 and
                    after['state']['tasks'] == protected['state']['tasks'], 'UI fallback replaced native spell work')
        else:
            require(not any('seq' in row or row.get('event') in ('module', 'trace-end', 'net-order') for row in rows),
                    'control has observer hooks')
        public.append([re.sub(r'tick=\d+ scene=(-?\d+) age=\d+ ', r'scene=\1 ', value) for value in markers])
    require(public[0] == public[1] == public[2], 'repeat/control public head contract differs')
    return dict(captures=2, controls=1, scenes=15, public_markers=216, smart_head=851971, attack_head=851983,
                engine_parity=False, remaining_owner='ORDER-02.2')
