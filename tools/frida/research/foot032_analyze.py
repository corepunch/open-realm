#!/usr/bin/env python3
"""FOOT-03.2: analyze a foot032 observer capture against the FOOT-03.1 eligibility model.

For every observed 1489a0 / 148e90 call on a shared cell C (site 0 (21,21), site 1 (41,21))
the most recent observer snapshot of C's lazy chain is fed to the independent predicate model
of tools/ghidra/research/verify_FOOT-03.1_eligibility.py; the observed return must match.
Also extracts per-phase JASS endpoint probe results, chain/reference counts and hierarchy class
words, and (optionally) compares JASS markers with an observer-free control or a repeat.
"""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('foot031', ROOT / 'ghidra' / 'research' / 'verify_FOOT-03.1_eligibility.py')
foot031 = importlib.util.module_from_spec(spec)
import sys  # noqa: E402
sys.path.insert(0, str(ROOT / 'ghidra' / 'research'))
spec.loader.exec_module(foot031)
SITES = {0: (21, 21), 1: (41, 21)}
MOVER_TAG = 0x60706375


def objects_from_chain(records):
    order, objs, links = [], [], []
    for rec in records:
        if rec['kind'] == 2:
            links.append((2, None))
            continue
        o = rec['obj']
        if o['object'] not in order:
            order.append(o['object'])
            objs.append(foot031.Obj(cat=o['w34'] & 0xffffff, active=bool(o['w34'] & 0x01000000),
                                    live='dead' if o['w38'] == 0xffffffff else 'live', flags=o['w40'],
                                    mover=o.get('payloadTag10') == MOVER_TAG))
        links.append((rec['kind'], order.index(o['object'])))
    return order, objs, links


def describe(records):
    out = {}
    for rec in records:
        if rec['kind'] == 2:
            continue
        o = rec['obj']
        out.setdefault(o['object'], dict(object=o['object'], w34=hex(o['w34']), dead=o['w38'] == 0xffffffff,
                                         refs3c=o['w3c'] & 0xffffff, w40=hex(o['w40']),
                                         payload_tag10=hex(o.get('payloadTag10', 0)), identity=o['identity']))
    return list(out.values())


def summarize(path):
    rows = [json.loads(line) for line in open(path)]
    markers = [r['value'] for r in rows if r.get('event') == 'marker']
    latest = {}
    window_state = {}
    pending = []
    checks, mismatches = [], []

    def evaluate(r, states):
        snap = states.get((r['x'], r['y']))
        if snap is None:
            return
        order, objs, links = objects_from_chain(snap[1]['records'])
        if r['event'] == 'fine-cell':
            target = next((i for i, a in enumerate(order) if int(a, 16) == r['a8']), None)
            predicted = foot031.model('fine', links, objs, r['a4'], r['d4'], target)['clear']
        else:
            predicted = foot031.model('hier', links, objs, r['a4'])['clear']
        row = dict(event=r['event'], window=r['window'], cell=[r['x'], r['y']], query=hex(r['a4']), mode=r['d4'],
                   caller=hex(r['caller']), observed=r['result'], predicted=predicted, snapshot=snap[0])
        checks.append(row)
        if row['observed'] != predicted:
            mismatches.append(row)
    phases = []
    for r in rows:
        ev = r.get('event')
        if ev == 'snapshot':
            for c in r['cells']:
                latest[(c['x'], c['y'])] = (r['marker'], c)
            for site, cell in SITES.items():
                if f'site={site}' in r['marker'] and any(t in r['marker'] for t in ('label=end-', 'label=probe-ground')):
                    c = next(c for c in r['cells'] if (c['x'], c['y']) == cell)
                    order, objs, links = objects_from_chain(c['records'])
                    phases.append(dict(marker=r['marker'], cell=cell, map_records_b0=r['map']['records_b0'],
                                       links=[(k, None if o is None else o) for k, o in links],
                                       objects=describe(c['records']),
                                       model={q: dict(fine_mode0=foot031.model('fine', links, objs, w, 0)['clear'],
                                                      fine_mode1=foot031.model('fine', links, objs, w, 1)['clear'],
                                                      hierarchy=foot031.model('hier', links, objs, h)['clear'],
                                                      collector=len(foot031.model('collect', links, objs, w)['tokens']))
                                              for q, w, h in (('ground', 0x02000002, 0x06000006), ('flight', 0x04000004, 0x04000004),
                                                              ('float', 0x40000040, 0x40000040), ('amph', 0x80000080, 0x80000080),
                                                              ('build', 0x08000008, 0x08000008), ('item', 0x10000010, 0x10000010))},
                                       union=hex(foot031.model('union', links, objs, 0)['union'])))
        if ev == 'snapshot' and ' label=end-' in r['marker'] and pending:
            # action windows: consumer calls run after the action, i.e. on the end-of-window state
            for call in pending:
                evaluate(call, {(c['x'], c['y']): (r['marker'], c) for c in r['cells']})
            pending = []
        if ev == 'marker' and ' label=begin-' in r['value']:
            window_state = dict(latest)
            pending = []
        if ev in ('fine-cell', 'hier-cell') and (r['x'], r['y']) in SITES.values():
            if 'label=begin-probe' in r['window']:
                # probe windows: calls on C precede any probe landing; use the pre-window state
                evaluate(r, window_state)
            else:
                pending.append(r)
    probes = [m for m in markers if ' label=probe-' in m]
    return dict(markers=markers, probes=probes, phases=phases, consumer_checks=len(checks), mismatches=mismatches,
                checks=checks, counts=next((r for r in rows if r.get('event') == 'trace-end'), {}),
                preload=next((r for r in rows if r.get('event') == 'preload-file'), {}))


def preload_markers(path):
    text = Path(path).read_text('utf-8', 'replace')
    return re.findall(r'call Preload\( "(FOOT032 [^"\r\n]*)" \)', text)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--compare-preload', type=Path, action='append', default=[],
                    help='other runs\' -preload.txt files (control/repeat) whose markers must equal this run')
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    s = summarize(args.capture)
    own = preload_markers(str(args.capture.with_suffix('')) + '-preload.txt')
    s['preload_markers'] = len(own)
    s['comparisons'] = []
    for other in args.compare_preload:
        theirs = preload_markers(other)
        s['comparisons'].append(dict(file=str(other), sha256=hashlib.sha256(Path(other).read_bytes()).hexdigest(),
                                     markers=len(theirs), equal=theirs == own,
                                     differences=[dict(index=i, this=a, other=b) for i, (a, b) in enumerate(zip(own, theirs)) if a != b]))
    s['capture_sha256'] = hashlib.sha256(args.capture.read_bytes()).hexdigest()
    s['analyzer_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.report.write_text(json.dumps(s, indent=1) + '\n')
    print(json.dumps(dict(markers=len(s['markers']), consumer_checks=s['consumer_checks'], mismatches=len(s['mismatches']),
                          comparisons=[(c['markers'], c['equal']) for c in s['comparisons']]), indent=None))
    return 1 if s['mismatches'] or any(not c['equal'] for c in s['comparisons']) else 0


if __name__ == '__main__':
    raise SystemExit(main())
