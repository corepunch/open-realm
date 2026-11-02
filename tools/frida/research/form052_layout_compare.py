#!/usr/bin/env python3
"""FORM-05.2 live layout versus production-C formation fixture (research tool, new file).

For every live 16a5b0 LayoutFormation call in form05/form052 observer captures, feeds the exact original inputs to
`pathing_formation_retail` of tools/ghidra/wc3_pathing_engine_probe.c (the composed/production C that matched 865
original raw-word cases) and compares all member offset words with the live words written by the original.

Input sources, labelled per case:
  exact-entry   form052_observer.js `layout-input` event read at 16a5b0 entry (pose, velocity, mover clock,
                owner clock [d53a48]+(14|68)+40, radius, rank (d8>>12)&15, heading +70), fixture word order.
  stationary    form05_observer.js captures: the group's last 16c150 snapshot before the layout, used only when every
                member velocity is zero (elapsed time then cannot change the predicted pose; clock words set to 0)
                and heading taken from the layout event. Moving cases without entry inputs are reported as skipped.
Build: cc -O2 -shared -fPIC -I <repo> tools/ghidra/wc3_pathing_engine_probe.c -o engine.so -lm
"""
import argparse, ctypes, hashlib, json
from pathlib import Path


def cases(path):
    last_tick, pending = {}, {}
    seq_tick = None
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        ev = r.get('event')
        if ev == 'marker' and ' tick=' in r['value']:
            seq_tick = int(r['value'].split(' tick=')[1].split()[0])
        elif ev == 'tick':
            last_tick[r['group']] = r
        elif ev == 'layout-input':
            pending[r['group']] = r
        elif ev == 'layout':
            live = [w for o in r['offsets'] for w in o[1]]
            entry = pending.pop(r['group'], None)
            if entry is not None:
                yield dict(seq=r['seq'], tick=seq_tick, source='exact-entry', caller=r['caller'], flags=entry['flags'],
                           heading_after=hex(r['heading']), heading_entry=hex(entry['input'][1]), input=entry['input'], live=live)
                continue
            snap = last_tick.get(r['group'])
            members = snap['members'] if snap else []
            if not snap or len(members) != len(r['offsets']) or any(m.get('vel') != [0, 0] for m in members):
                yield dict(seq=r['seq'], tick=seq_tick, source='skipped-moving-without-entry-inputs', caller=r['caller'], live=live)
                continue
            words = [len(members), r['heading']]
            for m in members:
                words += m['pos'] + m['vel'] + [m['time'], m['epoch'], 0, 0, 0, m['radius'], (m['d8'] >> 12) & 15]
            yield dict(seq=r['seq'], tick=seq_tick, source='stationary', caller=r['caller'], flags=snap['flags'],
                       heading_after=hex(r['heading']), input=words, live=live)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', nargs='+', type=Path)
    ap.add_argument('--engine', type=Path, action='append', required=True, help='engine probe .so (repeat for O0/O2)')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    libs = []
    for e in args.engine:
        lib = ctypes.CDLL(str(e.resolve()))
        lib.pathing_formation_retail.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
        libs.append((e.name, hashlib.sha256(e.read_bytes()).hexdigest(), lib))
    report = dict(task='FORM-05.2', engines=[[n, h] for n, h, _ in libs],
                  sources={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.captures}, captures=[])
    total = equal = 0
    for p in args.captures:
        rows = []
        for c in cases(p):
            if 'input' in c:
                n = c['input'][0]
                results = []
                for name, _, lib in libs:
                    out = (ctypes.c_uint32 * (1 + 2 * n))()
                    lib.pathing_formation_retail((ctypes.c_uint32 * len(c['input']))(*c['input']), out)
                    results.append(list(out))
                c['c_ok'] = results[0][0]
                c['c_offsets'] = [hex(w) for w in results[0][1:]]
                c['live_offsets'] = [hex(w) for w in c['live']]
                c['engines_agree'] = all(r == results[0] for r in results)
                c['equal'] = results[0][1:] == c['live'] and c['engines_agree'] and c['c_ok'] == 1
                c['input'] = [hex(w) for w in c['input']]
                total += 1
                equal += c['equal']
            else:
                c['live_offsets'] = [hex(w) for w in c['live']]
            del c['live']
            rows.append(c)
        report['captures'].append(dict(capture=p.name, layouts=rows))
        print(p.name, [(r['seq'], r['source'], r.get('equal')) for r in rows])
    report['compared'] = total
    report['equal'] = equal
    args.output.write_text(json.dumps(report, indent=1) + '\n')
    print('compared', total, 'equal', equal, hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
