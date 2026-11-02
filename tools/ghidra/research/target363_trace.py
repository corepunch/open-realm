#!/usr/bin/env python3
"""Read-only projection of the public mixed-owner target event transaction.

Other messages sent synchronously through the same callback object are retained
separately from actual deliveries of the dispatched target event. Pointer
identities become first-encounter logical identities; no scalar result is
rounded or replaced with an expected value.
"""
import json
from pathlib import Path

HANDLERS = {0x5fda10: 'Move', 0x49a5f0: 'Attack', 0x9d4aa0: 'Captain'}
EVENTS = {'remove': 0xd01a4, 'owner': 0xd01a2, 'death': 0xd01a4}


def load(path):
    return validate_rows([json.loads(line) for line in Path(path).read_text().splitlines()])


def validate_rows(rows):
    assert rows and rows[0]['event'] == 'metadata', 'missing capture metadata'
    assert not any(r.get('event') in ('trace-failed', 'error', 'script-error') or r.get('type') == 'error' for r in rows), 'failed capture'
    meta = rows[0]
    assert meta['task'] == 'GROUP-03.4.7.3-payoff363' and meta['owned'], 'capture owner/task'
    assert meta['sha256'] == 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236', 'retail image'
    assert rows[-1]['event'] == 'preload-file' and rows[-1]['complete'] and rows[-1]['markers'] == 302, 'incomplete preload'
    assert meta['mode'] in ('observe', 'control'), 'capture mode'
    if meta['mode'] == 'observe':
        assert sum(r.get('event') == 'trace-end' and r.get('installed') for r in rows) == 1, 'observer completion'
        markers = [r['value'] for r in rows if r.get('event') == 'marker']
        assert len(markers) == 302 and 'label=complete' in markers[-1], 'incomplete public markers'
    else:
        assert not any(r.get('event', '').startswith('registry-') or r.get('event') == 'module' for r in rows), 'control has hooks'
    return rows


def project(rows, variant):
    code = EVENTS[variant]
    before = f'label=mixed-{variant}-before'
    after = f'label=mixed-{variant}-after'
    start = next(i for i, r in enumerate(rows) if r.get('event') == 'marker' and before in r['value'])
    end = next(i for i, r in enumerate(rows[start + 1:], start + 1) if r.get('event') == 'marker' and after in r['value'])
    transaction = rows[start:end + 1]
    dispatches = [r for r in transaction if r.get('event') == 'registry-dispatch-enter' and r['code'] == code]
    assert len(dispatches) == 1, 'one synchronous mutation event'
    dispatch = dispatches[0]
    assert dispatch['depth'] == 0, 'unexpected outer dispatch depth'
    nodes = dispatch['nodes']
    identities = {}
    for frame in transaction:
        if frame.get('event') == 'registry-dispatch-enter':
            assert len({n['callback'] for n in frame['nodes']}) == len(frame['nodes']), 'duplicate event callback'
            for node in frame['nodes']:
                identities.setdefault(node['callback'], len(identities))
    if variant == 'death':
        deaths = [r for r in transaction if r.get('event') == 'registry-dispatch-enter' and r['code'] == 0xd01a0]
        assert len(deaths) == 1 and all(n['handler'] != 0x9d4aa0 for n in deaths[0]['nodes']), 'Captain unsubscribes before death packet'
    delivered = [r for r in transaction if r.get('event') == 'registry-callback-enter' and r['dispatch'] == dispatch['serial'] and r['code'] == code]
    assert [r['callback'] for r in delivered] == [n['callback'] for n in nodes], 'FIFO dispatch differs from registry ring'
    typed = [HANDLERS[r['handler']] for r in delivered if r['handler'] in HANDLERS]
    assert typed == ['Move'] + ['Attack'] * 6 + ['Captain', 'Attack', 'Move'], f'mixed producer history: {typed}'
    module = next(r for r in rows if r.get('event') == 'module')
    base = int(module['base'], 16)
    pointers = {}
    def normalize(value, key=None):
        if isinstance(value, dict):
            return {name: normalize(item, name) for name, item in value.items() if name != 'ms'}
        if isinstance(value, list):
            result = [normalize(item) for item in value]
            if key == 'current':
                assert len(result) == 4 and result[1] - base == 0xa9de14, 'Captain FloatMini vtable relocation'
                result[1] -= base
            return result
        if isinstance(value, str) and value.startswith('0x'):
            try:
                address = int(value, 16)
            except ValueError:
                return value
            if not address:
                return {'pointer': 0}
            if value not in pointers:
                pointers[value] = len(pointers) + 1
            return {'pointer': pointers[value]}
        return value
    # Preserve the whole synchronous mutation, including Inventory/Missile,
    # nested task messages, all subscriptions, caller results and exact clocks.
    # Only wall time and process pointer addresses require normalization.
    result = [normalize(row) for row in transaction]
    return {'variant': variant, 'code': code, 'typed_callback_order': typed,
            'transaction': result, 'public_markers': [r['value'] for r in rows if r.get('event') == 'marker']}


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('variant', choices=EVENTS)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    result = project(load(args.capture), args.variant)
    if args.output:
        assert not args.output.exists(), 'output must be new'
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    else:
        print(json.dumps(result, indent=2))
