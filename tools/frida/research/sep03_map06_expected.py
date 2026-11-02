#!/usr/bin/env python3
"""Compose frozen expected JSON for MAP-06.1 / MAP-06.2 from complete research captures.

  map061 <changelevel.jsonl> <restart.jsonl> <out.json>
  map062 <control> <save_only> <uiload> <ui_control> <ui_saveload1> <ui_saveload2-allmovers> <ctl_preload.txt>
         <saveload_control_preload.txt> <saveload_observe_preload.txt> <out.json>
Deterministic extraction only (no live access). Addresses are recorded as observed, not as expectations.
"""
import hashlib
import json
import sys
from pathlib import Path


def rows_of(path):
    return [json.loads(l) for l in Path(path).read_text().splitlines() if l.strip()]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def phases(rows):
    out, cur, movers = [], None, set()
    for i, r in enumerate(rows):
        e = r.get('event')
        if e == 'maps-create':
            cur = dict(maps=r['after'][:2], commits=[], teardown=[], release=None)
            out.append(cur)
            movers = set()
        if cur is None:
            continue
        if e == 'mover-activate' and r.get('name') == 'M':
            movers.add(r['mover'])
        if e == 'mover-commit' and r['mover'] in movers:
            cur['commits'].append([r['tick'], *r['pos'], *r['vel']])
        if e == 'mover-retire':
            cur['teardown'].append(r['caller'])
        if e == 'sobj-retire' and r.get('flags') == '10000000':
            cur['teardown'].append('region:' + r['caller'])
        if e == 'spatial-map-release':
            cur.setdefault('spatial_release', []).append(dict(kind=r['before']['kind'], records_before=r['before']['records'],
                                                              records_after=r['after']['records'],
                                                              link_capacity_after=r['after']['capacity']))
    return out


def compress(seq):
    res = []
    for c in seq:
        if res and res[-1][0] == c:
            res[-1][1] += 1
        else:
            res.append([c, 1])
    return res


def map061(cl, rs, out):
    payload = dict(task='MAP-06.1', captures={Path(cl).name: sha(cl), Path(rs).name: sha(rs)}, flows={})
    for name, path in (('changelevel', cl), ('restart', rs)):
        ph = phases(rows_of(path))
        ref = ph[0]['commits']
        cycles = []
        for p in ph:
            s = p['commits']
            n = min(len(ref), len(s))
            eq = next((j for j in range(n) if ref[j][1:] != s[j][1:]), n)
            cycles.append(dict(observed_maps=p['maps'], commits=len(s), equal_prefix_with_first=eq, tail=s[eq:eq + 3],
                               teardown_order=compress(p['teardown']), spatial_release=p.get('spatial_release')))
        payload['flows'][name] = dict(reference_first_movement=ref, cycles=cycles)
    Path(out).write_text(json.dumps(payload, indent=1) + '\n')
    print(sha(out))


def unit_sequences(rows, after_load=False):
    names, seqs = {}, {}
    load_end = next((i for i, r in enumerate(rows) if r.get('event') == 'owner-load-end'), None)
    for i, r in enumerate(rows):
        if r.get('event') == 'mover-activate' and r.get('name'):
            names[r['mover']] = r['name']
        if r.get('event') == 'mover-commit':
            if after_load and load_end is not None and i > load_end:
                seqs.setdefault('loaded:' + r['mover'], []).append([r['tick'], *r['pos'], *r['vel']])
            elif r['mover'] in names and (load_end is None or i < load_end):
                seqs.setdefault(names[r['mover']], []).append([r['tick'], *r['pos'], *r['vel']])
    return seqs


def marker_lines(path):
    """RSPATIAL/RSCROWD strings from a PreloadGenEnd file, in file order."""
    import re
    return re.findall(r'(RS(?:PATIAL|CROWD) tick=[^"]*)', Path(path).read_text(errors='replace'))


def chain_rule(rows):
    """Proximity chains (cells with >1 entry) at save and after load, as save indices (save order == load order)."""
    snaps = [r for r in rows if r.get('event') == 'snapshot']
    saves = [r for r in rows if r.get('event') == 'sobj-save']
    loads = [r for r in rows if r.get('event') == 'sobj-load']
    saves = saves[len(saves) - len(loads):]  # the save that was loaded is the last one (uiload also has an earlier scripted save)
    sid = {s['obj']: i for i, s in enumerate(saves)}
    lid = {l['obj']: i for i, l in enumerate(loads)}
    names = {i: s['name'] for i, s in enumerate(saves) if s.get('name')}

    def chains(label, ids):
        snap = [x for x in snaps if x['label'] == label][-1]
        return ({str(c['cell']): [ids.get(x[2], x[2]) for x in c['chain']] for c in snap['proximity']['chains'] if len(c['chain']) > 1},
                {m: {k: snap[m]['state'][k] for k in ('records', 'linkCount', 'freeHead', 'stamp')} for m in ('proximity', 'fine')})
    before, sb = chains('after-owner-save', sid)
    after, sa = chains('after-owner-load', lid)
    return dict(objects=len(loads),
                save_order_equals_load_order=len(saves) == len(loads) and all(
                    s['rect'] == l['rect'] and s['refs'] == l['refs'] and s['stamp'] == l['stamp'] and s['flags'] == l['flags']
                    for s, l in zip(saves, loads)),
                named_save_indices=names, chains_at_save=before, chains_after_load=after,
                after_load_chains_descending_save_index=all(v == sorted(v, reverse=True) for v in after.values()),
                state_at_save=sb, state_after_load=sa,
                map_loads=[dict(kind=r['after']['kind'], records=r['after']['records'], linkCount=r['after']['linkCount'],
                                stamp=r['after']['stamp']) for r in rows if r.get('event') == 'map-load'])


def resumed(control_rows, rows):
    """Match each loaded mover to a control unit by (tick, position) of its first post-load commit; compare exactly."""
    c = unit_sequences(control_rows)
    lm = {k: v for k, v in unit_sequences(rows, after_load=True).items() if k.startswith('loaded:')}
    out = {}
    for m, seq in lm.items():
        hit = next(((n, i) for n, a in c.items() for i, t in enumerate(a) if t[0] == seq[0][0] and t[1:3] == seq[0][1:3]), None)
        if hit is None:
            out[m] = dict(matched=None, commits=len(seq), first=seq[0])
            continue
        n, i = hit
        ref = c[n][i:]
        k = next((j for j in range(min(len(ref), len(seq))) if ref[j] != seq[j]), None)
        out[m] = dict(matched=n, control_index=i, resume_tick=seq[0][0], control_commits=len(ref), loaded_commits=len(seq),
                      first_difference=k, equal=k is None and len(ref) == len(seq), sequence=seq)
    return out


def map062(control, save_only, uiload, ui_control, ui_saveload1, ui_saveload2, ctl_preload, sl_ctl_preload, sl_obs_preload, out):
    files = (control, save_only, uiload, ui_control, ui_saveload1, ui_saveload2, ctl_preload, sl_ctl_preload, sl_obs_preload)
    payload = dict(task='MAP-06.2', captures={Path(p).name: sha(p) for p in files})
    a, b = unit_sequences(rows_of(control)), unit_sequences(rows_of(save_only))
    payload['scripted_save_continue'] = {n: dict(control=len(a[n]), save=len(b.get(n, [])), equal=a[n] == b.get(n))
                                         for n in sorted(a)}
    payload['scripted_control_sequences'] = a
    payload['idle_ui_load'] = chain_rule(rows_of(uiload))
    c_rows = rows_of(ui_control)
    s2 = rows_of(ui_saveload2)
    c = unit_sequences(c_rows)
    pre = unit_sequences(s2)
    payload['active_route_ui_save_load'] = dict(
        save_tick=next(r['tick'] for r in s2 if r.get('event') == 'owner-save-begin'),
        load_tick_before_load=next(r['tick'] for r in s2 if r.get('event') == 'owner-load-begin'),
        presave_prefix_equal={n: c[n] == pre[n][:len(c[n])] for n in sorted(c)},
        presave_extra_tail={n: pre[n][len(c[n]):] for n in sorted(c)},
        resumed=resumed(c_rows, s2),
        chains=chain_rule(s2))
    payload['ui_route_control_sequences'] = c
    cm, sm, om = marker_lines(ctl_preload), marker_lines(sl_ctl_preload), marker_lines(sl_obs_preload)
    def after_first_pass(lines):
        seen, res = set(), []
        for l in lines:
            if l in seen or res:
                res.append(l)
            seen.add(l)
        return res
    payload['jass_markers'] = dict(control=len(cm), saveload_control=len(sm), saveload_observe=len(om),
                                   first_pass_equal_control=dict(saveload_control=sm[:len(cm)] == cm, saveload_observe=om[:len(cm)] == cm),
                                   resumed_lines_control=len(sm) - len(cm), resumed_lines_observe=len(om) - len(cm),
                                   resumed_equal_control_suffix=dict(
                                       saveload_control=sm[len(cm):] == cm[len(cm) - (len(sm) - len(cm)):],
                                       saveload_observe=om[len(cm):] == cm[len(cm) - (len(om) - len(cm)):]),
                                   saveload_control_equals_observe=sm == om)
    Path(out).write_text(json.dumps(payload, indent=1) + '\n')
    print(sha(out))


if __name__ == '__main__':
    if sys.argv[1] == 'map061':
        map061(*sys.argv[2:5])
    elif sys.argv[1] == 'map062':
        map062(*sys.argv[2:12])
    else:
        raise SystemExit(__doc__)
