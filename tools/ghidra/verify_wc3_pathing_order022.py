#!/usr/bin/env python3
"""Audit recovered ORDER-02.2 captures without treating inventory as engine parity.

Literal JSONL and CRLF preload bytes survive in the bundle. Producer timing
varies with OS input delivery; compare the public event contract separately from
those literal timestamps. This gate does not certify arbitrary queue lifetimes.
"""
import argparse
import base64
import gzip
import hashlib
import json
import itertools
import re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes
ROOT = Path(__file__).resolve().parents[2]
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-order022-live-1.27.json.gz'
CONTRACTS = ROOT / 'tools/ghidra/fixtures/retail-order022-contracts-1.27.json.gz'
PAIRED = {'set-owner', 'admit', 'intercept', 'append', 'insert', 'dispatch',
          'cancel-successors', 'cancel-all', 'net-order', 'fallback-test', 'begin-channel-work'}
EVENTS = {'point-event', 'target-event', 'immediate-event'}
MAPS = {'owner': '323667265c689682d09a850a0370d8ed2e2785fb0f14226238468bf11b62c02f',
        'ui': 'e655b4f8fedc2e159ecfd5687a0e0c6d11c7ab070f134733f8a9c65bcd5aecbe'}
HELPERS = {'order022-ui.exe': 'ff926b393c86d9dec0f8f1f6d764b4f2875af42af6d60b2b69e68b31544db002',
           'order022-ui.exe.so': '46223cc7faf71421afc41d74db53e0cb72603b1e2e4aa9aabfb8c7bab2dc5dbe'}


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def public_contract(values):
    result = []
    for value in values:
        if 'label=sample' in value or 'event=852056 ' in value:
            continue
        value = re.sub(r'tick=\d+ ', '', value)
        value = re.sub(r'age=\d+ ', '', value)
        result.append(value)
    return result


def scenes(rows):
    result = [[] for _ in range(7)]
    scene = -1
    for row in rows:
        if row['event'] == 'marker':
            found = re.search(r'scene=(-?\d+) ', row['value'])
            if found:
                scene = int(found[1])
        if 0 <= scene < 7:
            result[scene].append(row)
    return result


def unwind(rows):
    seq = [r['seq'] for r in rows if 'seq' in r]
    require(seq == list(range(1, len(seq) + 1)), 'noncontiguous observer sequence')
    stack = []
    for row in rows:
        event = row['event']
        if event in PAIRED:
            stack.append((event, row['unit']))
            require(row['depth'] == len(stack), 'wrong entry depth')
        elif event.endswith('-end') and event[:-4] in PAIRED:
            require(stack and stack[-1] == (event[:-4], row['unit']), 'unpaired callback')
            require(row['depth'] == len(stack), 'wrong exit depth')
            stack.pop()
    require(not stack, 'unfinished callback')
    end = [r for r in rows if r['event'] == 'trace-end']
    require(len(end) == 1 and end[0] == {'event': 'trace-end', 'installed': True, 'depth': 0},
            'incomplete trace')


def owner_native(parts):
    for scene in range(4):
        rows = parts[scene]
        enter = [i for i, r in enumerate(rows) if r['event'] == 'set-owner']
        require(len(enter) == 1, 'missing owner change')
        at = enter[0]
        end = next(i for i in range(at + 1, len(rows)) if rows[i]['event'] == 'set-owner-end')
        rr = rows[at:end + 1]
        a = [r for r in rr if r['event'] == 'admit']
        require(len(a) == 1 and a[0]['order']['command'] == 851972 and
                a[0]['mode'] == a[0]['dispatch'] == 1 and a[0]['caller'] == 0x698d92,
                'owner did not admit ordinary Stop')
        stop = [i for i, r in enumerate(rr) if r['event'] == 'immediate-event' and r['order']['command'] == 851972]
        owned = [i for i, r in enumerate(rr) if r['event'] == 'marker' and 'label=owner-event ' in r['value']]
        require(len(stop) == len(owned) == 1 and stop[0] < owned[0], 'Stop/owner event order differs')
        require(all(r['state']['owner'] == 3 for r in rr[:stop[0] + 1] if 'state' in r), 'old owner not retained')
        require(rr[-1]['state']['owner'] == 1 and rr[-1]['state']['count'] == 0 and
                rr[-1]['state']['head_order'] is None, 'owner transition retained live work')
        before = rr[0]['state']
        require(before['count'] == [0, 1, 1, 3][scene], 'missing idle/walk/attack/queued start')
    for scene in (4, 5):
        rows = parts[scene]
        matches = [i for i, r in enumerate(rows) if r['event'] == 'intercept-end' and r['result'] == 1]
        require(len(matches) == 1, 'missing working Repair interception')
        at = matches[0]
        before = rows[at]['state']
        cancel = rows[at + 1:at + 3]
        require([r['event'] for r in cancel] == ['cancel-successors', 'cancel-successors-end'],
                'interception did not cancel successors')
        after = cancel[-1]['state']
        require(before['count'] == 2 and after['count'] == 1 and before['user'] == after['user'] and
                before['tasks'] == after['tasks'] and after['head_order']['command'] == 852024 and
                after['tail'] == after['user'] and cancel[0]['caller'] == 0x680383,
                'interception replaced retained head/task or kept successors')
        # No second target event, nor delayed Move when Repair finishes.
        require([r['order']['command'] for r in rows if r['event'] in EVENTS and r['order']['command'] != 852056] == [852024],
                'interception notified or activated a canceled successor')
    require([r['order']['command'] for r in parts[6] if r['event'] in EVENTS and r['order']['command'] != 852056] == [852024, 851986],
            'untouched Repair successor never activated')


def ui_native(parts):
    for scene, rows in enumerate(parts):
        packets = [r for r in rows if r['event'] == 'net-order']
        expected = 2 if scene in (3, 5, 6) else 1
        require(len(packets) == expected, 'missing selected UI recipients')
        if scene in (0, 2, 3, 5):
            protected = [r for r in packets if r['fallback'] == 1]
            require(len(protected) == 1 and protected[0]['state']['head_order']['command'] == 852600,
                    'missing protected spell head')
            p = protected[0]
            require(bool(p['state']['head_order']['flags'] & 0x40) == (scene != 5), 'wrong cast protection phase')
            if scene == 2:
                require(p['flags'] & 1, 'missing Shift control')
                append = [r for r in rows if r['event'] == 'append' and r['order']['command'] == 851986]
                require(len(append) == 1 and append[0]['order']['flags'] & 4, 'Shift did not mark and append')
            else:
                insert = [r for r in rows if r['event'] == 'insert']
                require(len(insert) == 1 and insert[0]['after_head'] == 1 and insert[0]['dispatch'] == 0,
                        'fallback was not a silent insertion behind the head')
                at = rows.index(insert[0])
                after = rows[at + 1]
                require(after['event'] == 'insert-end' and after['state']['count'] == 2 and
                        after['state']['user'] == insert[0]['state']['user'] and
                        after['state']['tasks'] == insert[0]['state']['tasks'], 'fallback replaced active work')
            finish = [i for i, r in enumerate(rows) if r['event'] == 'marker' and 'phase=finish ' in r['value']]
            move = [i for i, r in enumerate(rows) if r['event'] == 'point-event' and
                    r['order']['command'] == 851986 and r['unit'] == p['unit']]
            require(len(finish) == len(move) == 1 and finish[0] < move[0], 'protected command issued prematurely')
        else:
            require(all(r['fallback'] == 0 for r in packets), 'Stop/Hold/all-working exception disappeared')


def audit(bundle=None):
    bundle = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(len(bundle['captures']) == 6, 'two repeats/control per family required')
    public = {'owner': [], 'ui': []}
    total = 0
    for index, capture in enumerate(bundle['captures']):
        family = 'owner' if index < 3 else 'ui'
        mode = 'control' if index % 3 == 2 else 'observe'
        raw = base64.b64decode(capture['raw_b64'], validate=True)
        preload = base64.b64decode(capture['preload_b64'], validate=True)
        require(hashlib.sha256(raw).hexdigest() == capture['raw_sha256'] and
                hashlib.sha256(preload).hexdigest() == capture['preload_sha256'], 'literal bytes changed')
        rows = [json.loads(l) for l in raw.splitlines()]
        meta, footer = rows[0], rows[-1]
        require(capture['family'] == family and meta['mode'] == mode and meta['sha256'] == SHA and
                meta['task'] == 'ORDER-02.2' and meta['owned'] and meta['env'] in ('B', 'C') and
                meta['display'] == {'B': ':98', 'C': ':99'}[meta['env']] and
                meta['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[meta['env']], 'unowned or changed capture')
        require(meta['source_sha256']['map'] == MAPS[family] and
                all(meta['source_sha256'][name] == digest for name, digest in HELPERS.items()),
                'authored map or native input helper differs')
        for name in ('order022_capture.py', 'order022_observer.js', 'order022_make_map.py', 'order022_probe.j',
                     'order022_ui_input.c', 'order0110_ui_input.c'):
            require(hashlib.sha256((ROOT / 'tools/frida/research' / name).read_bytes()).hexdigest() == meta['source_sha256'][name],
                    'capture source mismatch: ' + name)
        values = re.findall(r'call Preload\( "(O022 [^"\r\n]*)" \)', preload.decode())
        require(footer == dict(event='preload-file', sha256=capture['preload_sha256'], markers=len(values), complete=True)
                and len(values) == (256 if family == 'owner' else 252), 'incomplete public stream')
        require(values[-1].endswith('scene=7 age=' + ('450' if family == 'owner' else '220') + ' label=complete variant=' + family),
                'missing terminal marker')
        require(not any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows), 'capture failed')
        inputs = [r for r in rows if r['event'] == 'player-input']
        require(len(inputs) == (6 if family == 'owner' else 7) and all(r['rc'] == 0 for r in inputs), 'input plan incomplete')
        if mode == 'observe':
            unwind(rows)
            observed = [r['value'] for r in rows if r['event'] == 'marker' and 'label=start ' not in r['value']]
            require(observed == values, 'observer and preload disagree')
            (owner_native if family == 'owner' else ui_native)(scenes(rows))
        else:
            require(not any('seq' in r or r['event'] == 'trace-end' for r in rows), 'control was hooked')
        public[family].append(public_contract(values))
        total += len(values)
    for family, streams in public.items():
        require(streams[0] == streams[1] == streams[2], 'public event contract differs: ' + family)
    return dict(captures=4, controls=2, scenes=42, public_markers=total,
                engine_parity=False, remaining_owner='ORDER-02.2')


def guards():
    contracts = json.loads(gzip.decompress(CONTRACTS.read_bytes()))
    result = {}
    def walk(value):
        if isinstance(value, dict):
            address = value.get('site')
            raw = value.get('bytes') or value.get('call_bytes')
            if address and isinstance(raw, str) and re.fullmatch(r'[0-9a-fA-F]+', raw):
                result[address.removeprefix('0x')] = raw
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
    walk(contracts)
    require(len(result) == 371, 'missing static producer/caller guards')
    return result


def admission_contract(data=None):
    path = ROOT / 'tools/ghidra/fixtures/retail-order022-admission-1.27.json.gz'
    data = data if data is not None else json.loads(gzip.decompress(path.read_bytes()))
    universe = list(itertools.product((0, 0x100), (-1, 0, 1, 2), (0, 1), (False, True), (False, True), (0, 8)))
    require([tuple(row[k] for k in ('dead', 'depth', 'mode', 'intercept', 'present', 'drop'))
             for row in data['admission']] == universe, 'incomplete admission control matrix')
    for row in data['admission']:
        if row['dead']:
            names = []
        elif row['depth'] > 0:
            names = ['cancel-successors', 'append'] if row['mode'] else ['insert']
        elif row['intercept']:
            names = ['intercept'] + (['cancel-successors'] if row['mode'] else [])
        elif row['mode']:
            names = ['intercept', 'cancel-all', 'append']
        else:
            names = ['intercept', 'cancel-internal'] + (['resolve-head'] + (['pop-head'] if row['drop'] else [])
                     if row['present'] else []) + ['insert']
        require([e['name'] for e in row['events']] == names, 'wrong original admission branch')
        for event in row['events']:
            if event['name'] == 'insert':
                require(event['args'] == [0x10003000, int(row['depth'] > 0), 1], 'insertion ABI differs')
    require([tuple(row[k] for k in ('widget', 'dead', 'owner')) for row in data['owner']] ==
            list(itertools.product((0, 1), (0, 0x100), (0, 3, 15))), 'incomplete owner gate matrix')
    for row in data['owner']:
        events = row['events']
        if row['widget'] or row['dead']:
            require(events == [dict(name='cancel-successors', args=[])], 'inactive owner gate dispatched')
        else:
            require([e['name'] for e in events] == ['create-immediate', 'intercept', 'cancel-all', 'append'] and
                    events[0] == dict(name='create-immediate', args=[0], command=851972, owner=row['owner']),
                    'owner Stop command/issuer differs')
    return data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    parser.add_argument('--evidence-only', action='store_true')
    args = parser.parse_args()
    require(not args.report.exists(), 'fresh report required')
    result = audit()
    from audit_wc3_pathing_order022_smart import audit as smart_head_audit
    result["smart_head"] = smart_head_audit()
    from audit_wc3_pathing_order022_selected_target import audit as selected_target_audit
    result["selected_target"] = selected_target_audit()
    admission = admission_contract()
    instruction_guards = guards()
    if args.binary:
        original_bytes(args.binary, instruction_guards)
        sys_path = str(ROOT / 'tools/ghidra/research')
        import sys
        sys.path.insert(0, sys_path)
        from order022_admission_oracle import original
        require(original(args.binary) == admission, 'fresh original admission execution differs')
        from order022_ui_oracle import original as original_selector, compare_engine
        selector = original_selector(args.binary)
        result['ui_selector'] = dict(native_cases=len(selector['selector']),
                                     selection_masks=len(selector['accumulator']),
                                     engine=compare_engine(selector, ROOT))
    result.update(instruction_guards=len(instruction_guards), binary_checked=bool(args.binary),
                  binary_sha256=SHA if args.binary else None,
                  admission_cases=len(admission['admission']), owner_slices=len(admission['owner']))
    if not args.evidence_only:
        import verify_wc3_pathing_work242 as runner
        saved = runner.TESTS
        # The engine supports trailing prefix wildcards, not an interior *smart*.
        # Expand registered names so every Smart movement control executes.
        movement = (ROOT / 'games/warcraft-3/game/tests/t_movement.c').read_text()
        smart = re.findall(r'TEST\(wc3_movement,\s*(\w*smart\w*)\s*\)', movement)
        require(len(smart) >= 33 and len(set(smart)) == len(smart), 'missing registered Smart movement controls')
        runner.TESTS = ['wc3_order022.*', 'wc3_order_lifecycle.*', 'wc3_unit.*', 'wc3_movement.trained*',
                        'wc3_order022_ui.*', 'wc3_items.teleport327*'] + ['wc3_movement.' + name for name in smart]
        try:
            result['engine'] = runner.run_engine(args.test_binary, args.data, args.report)
        finally:
            runner.TESTS = saved
        result['test_binary_sha256'] = hashlib.sha256(args.test_binary.read_bytes()).hexdigest()
        result['game_library_sha256'] = hashlib.sha256((args.test_binary.parent.parent / 'lib/libgame-wc3-test.so').read_bytes()).hexdigest()
    result['engine_checked'] = not args.evidence_only
    result['passed'] = bool(args.binary and result['engine_checked'])
    result['status'] = 'verified' if result['passed'] else 'evidence-only'
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
