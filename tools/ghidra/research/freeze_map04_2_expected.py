#!/usr/bin/env python3
"""Merge the MAP-04.2 frozen evidence into expected-MAP-04.2.json (deterministic).

Inputs: the original-code exit/edit oracle payload, the static scope-exit report and
the live capture analyses. Only stable fields are kept (no sequence numbers/pointers).
"""
import argparse, hashlib, json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--oracle', type=Path, required=True)
    ap.add_argument('--static', type=Path, required=True)
    ap.add_argument('--live', type=Path, nargs='+', required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    oracle = json.loads(a.oracle.read_text())
    static = json.loads(a.static.read_text())
    scopes = [dict(name=s['name'], entry=s['entry'], acquires=s['acquires'], releases=s['releases'],
                   exits_while_held=s['exits_while_held'], guards=s['guards'],
                   direct_calls_while_held=s['direct_calls_while_held'], closure_functions=s['closure_functions'],
                   closure_indirect=s['closure_indirect'], closure_imports=sorted({t for _, _, t in s['closure_imports']}),
                   writers_or_scopes_reachable=s['writers_or_scopes_reachable'], eh=s['eh']) for s in static['scopes']]
    live = []
    for p in a.live:
        r = json.loads(p.read_text())
        s = r['summary']
        live.append(dict(capture=Path(r['capture']).name, capture_sha256=r['capture_sha256'], map=r['metadata']['map'],
                         map_sha256=r['metadata']['map_sha256'], observer_sha256=r['metadata']['observer_sha256'],
                         complete=s['complete'], lifo_violations=s['lifo_violations'], fine_requests=s['fine_requests'],
                         fine_restored=s['fine_restored'], coarse_requests=s['coarse_requests'],
                         coarse_order_ok=s['coarse_order_ok'], coarse_restored_equal=s['coarse_restored_equal'],
                         coarse_cleared_at_search=s['coarse_cleared_at_search'],
                         foreign_writers_inside_request_scopes=s['foreign_writers_inside_request_scopes'],
                         nesting=s['nesting'], reference_marker_equal=s['reference_marker_equal'],
                         coarse=[dict(caller=c['caller'], self=c['self'] and c['self']['rect'], target=c['target'] and c['target']['rect'],
                                      changed_after_request=c['changed_after_request']) for c in r['coarse_requests']],
                         terrain_edit_publication=[{k: v for k, v in e.items() if k != 'first_request'}
                                                   for e in r['terrain_edit_publication']]))
    payload = dict(version=1, binary_sha256=oracle['binary_sha256'],
                   oracle_sha256=hashlib.sha256(a.oracle.read_bytes()).hexdigest(), oracle=oracle,
                   static_sha256=hashlib.sha256(a.static.read_bytes()).hexdigest(), static_tool=static['tool'],
                   static_scopes=scopes, live=live)
    blob = json.dumps(payload, sort_keys=True, separators=(',', ':')) + '\n'
    a.output.write_text(blob)
    print(hashlib.sha256(blob.encode()).hexdigest())


if __name__ == '__main__':
    main()
