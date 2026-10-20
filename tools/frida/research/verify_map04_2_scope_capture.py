#!/usr/bin/env python3
"""Analyze MAP-04.1/04.2 live exclusion-scope captures (map04_2_scope_observer.js).

Checks, per complete capture: LIFO scope pairing; fine request self/target +40
increments visible at the search and restored at exit, with every other object
linked in the target rectangle unchanged; coarse request 15d360 call order,
rounded-coverage clearing at the search and class-byte restoration; writers or
other scopes entered while a request scope is active (edit-during-request);
nesting pairs; and JASS marker equality with a reference and/or control run.
"""
import argparse, collections, hashlib, json, re
from pathlib import Path


def markers_from_preload(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    return re.findall(r'call Preload\( "(PATH[A-Z]+ [^"\r\n]*)" \)', text)


def load(path):
    return [json.loads(l) for l in path.read_text().splitlines() if l.strip()]


def classes_by_cell(snapshot):
    out = {}
    for lev in snapshot or []:
        for dy, row in enumerate(lev['rows']):
            for dx in range(len(row) // 2):
                out[(lev['level'], lev['x0'] + dx, lev['y0'] + dy)] = int(row[2 * dx:2 * dx + 2], 16)
    return out


def coverage(rect, level):
    s = 2 << level
    y0, x0, y1, x1 = rect
    return {(x, y) for y in range(y0 // s, y1 // s + 1) for x in range(x0 // s, x1 // s + 1)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True, help='capture directory')
    ap.add_argument('--reference', type=Path, help='reference jsonl with marker events (prior observer capture)')
    ap.add_argument('--control', type=Path, help='observer-free control capture directory')
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    rows = load(a.capture / 'capture.jsonl')
    meta = rows[0]
    end = [r for r in rows if r.get('event') == 'trace-end']
    failed = [r for r in rows if r.get('event') == 'trace-failed' or r.get('type') == 'error']
    report = dict(capture=str(a.capture), capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(),
                  metadata=meta, complete=bool(end) and not failed, failures=failed[:5], counts=end[0]['counts'] if end else None)
    lifo = [r for r in rows if r.get('event') == 'scope-lifo-violation']
    report['lifo_violations'] = len(lifo)
    enters = {(r['scope'], r['id']): r for r in rows if r.get('event') == 'scope-enter'}
    leaves = {(r['scope'], r['id']): r for r in rows if r.get('event') == 'scope-leave'}
    searches = collections.defaultdict(list)
    for r in rows:
        if r.get('event') in ('fine-search', 'coarse-search'):
            searches[r['id']].append(r)
    # Writers grouped by the innermost active scope id.
    writers = [r for r in rows if r.get('event') == 'writer-enter']
    writer_leaves = {}
    for r in rows:
        if r.get('event') == 'writer-leave':
            writer_leaves.setdefault((r['writer'], r['n']), r)
    inside = collections.Counter()
    for w in writers:
        if w['scopes']:
            inside[(w['scopes'][-1].split('#')[0], w['writer'])] += 1
    report['writers_inside_scopes'] = {f'{s} -> {w}': n for (s, w), n in sorted(inside.items())}
    request_scopes = {'coarse_request', 'fine_request', 'visible_waypoint', 'next_step_blockers'}
    report['foreign_writers_inside_request_scopes'] = [
        w for w in writers if w['scopes'] and w['scopes'][-1].split('#')[0] in request_scopes
        and not (w['writer'] == 'hierarchy_rectangle' and w['scopes'][-1].startswith('coarse_request'))]
    nesting = collections.Counter()
    for r in enters.values():
        if r['depth']:
            nesting[' > '.join(r['outer'] + [r['scope']])] += 1
    report['nesting'] = dict(nesting)
    report['terrain_natives'] = [dict(x=r['x'], y=r['y'], type=r['pathingType'], passable=r['passable'], scopes=r['scopes'])
                                 for r in rows if r.get('event') == 'terrain-native']
    # Fine request counter checks.
    fine = []
    for (scope, i), e in enters.items():
        if scope != 'fine_request' or 'self' not in e:
            continue
        l = leaves.get((scope, i))
        s = searches.get(i, [])
        row = dict(id=i, caller=e['caller'], result=l and l['result'], self=e['self'], target=e['target'])
        objs = [k for k in ('self', 'target') if e[k]]
        same = e['self'] and e['target'] and e['self']['object'] == e['target']['object']
        ok = l is not None and all(l[k]['word40'] == e[k]['word40'] for k in objs)
        if s:
            for k in objs:
                want = (int(e[k]['word40'], 16) + (2 if same else 1)) & 0xffffffff
                ok &= int(s[0][k]['word40'], 16) == want
            if e.get('targetLinks') and s[0].get('targetLinks') is not None:
                before = {o['object']: o['word40'] for o in e['targetLinks']}
                during = {o['object']: o['word40'] for o in s[0]['targetLinks']}
                after = {o['object']: o['word40'] for o in l['targetLinks']} if l and l.get('targetLinks') else {}
                excluded = {e[k]['object'] for k in objs}
                others = sorted(set(before) - excluded)
                row['target_rect_objects'] = len(before)
                row['other_overlapping_objects'] = [dict(object=o, before=before[o], search=during.get(o), after=after.get(o)) for o in others]
                ok &= all(during.get(o) == before[o] == after.get(o) for o in others)
                row['target_links'] = dict(before=e['targetLinks'], search=s[0]['targetLinks'])
        row['searched'] = bool(s)
        row['restored'] = ok
        fine.append(row)
    report['fine_requests'] = fine
    # Coarse request checks.
    coarse = []
    by_scope = collections.defaultdict(list)
    for w in writers:
        if w['writer'] == 'hierarchy_rectangle' and w['scopes'] and w['scopes'][-1].startswith('coarse_request'):
            by_scope[int(w['scopes'][-1].split('#')[1])].append(w)
    for (scope, i), e in enters.items():
        if scope != 'coarse_request' or 'self' not in e:
            continue
        l = leaves.get((scope, i))
        calls = by_scope.get(i, [])
        objs = [e[k] for k in ('self', 'target') if e[k]]
        expected = [(o['rect'], 1) for o in objs] + [(o['rect'], 0) for o in objs]
        order_ok = [(c['rect'], c['mode']) for c in calls] == expected
        before, after = classes_by_cell(e.get('classes')), classes_by_cell(l.get('classes') if l else None)
        changed = sorted([*k, before[k], after.get(k)] for k in before if after.get(k) != before[k])
        s = searches.get(i, [])
        cleared_ok = None
        if s and s[0].get('classes'):
            during = classes_by_cell(s[0]['classes'])
            cov = {(lev, x, y) for o in objs for (x, y) in coverage(o['rect'], 0) for lev in [0]}
            cleared_ok = all(during.get(k, 0) == 0 for k in cov if k in during)
        steps = []
        for c in calls:
            wl = writer_leaves.get(('hierarchy_rectangle', c['n']))
            b, a2 = classes_by_cell(c.get('classes')), classes_by_cell(wl.get('classes') if wl else None)
            steps.append(dict(mode=c['mode'], rect=c['rect'], word40=c['objectWord40'],
                              changed=sorted([*k, b[k], a2.get(k)] for k in b if a2.get(k) != b[k])))
        coarse.append(dict(id=i, caller=e['caller'], result=l and l['result'], self=e['self'], target=e['target'],
                           call_order_ok=order_ok, cleared_at_search=cleared_ok,
                           restored_equal=not changed, changed_after_request=changed, steps=steps))
    report['coarse_requests'] = coarse
    # Pending terrain edits (flat research maps: world origin 0, 32 world units per fine cell)
    # versus the first later coarse request whose snapshot window contains their base cell.
    edits = []
    for t in report['terrain_natives']:
        fx, fy = int(t['x'] // 32), int(t['y'] // 32)
        row = dict(world=[t['x'], t['y']], fine=[fx, fy], base=[fx // 2, fy // 2], scopes=t['scopes'])
        for c in coarse:
            e = enters[('coarse_request', c['id'])]
            l = leaves.get(('coarse_request', c['id']))
            b, a2 = classes_by_cell(e.get('classes')), classes_by_cell(l.get('classes') if l else None)
            k = (0, fx // 2, fy // 2)
            if k in b:
                objs = [e[x] for x in ('self', 'target') if e[x]]
                inside = any((fx // 2, fy // 2) in coverage(o['rect'], 0) for o in objs)
                row.update(first_request=c['id'], in_rounded_coverage=inside, base_before=b[k], base_after=a2.get(k))
                break
        edits.append(row)
    report['terrain_edit_publication'] = edits
    # Markers.
    observed = [r['value'] for r in rows if r.get('event') == 'marker']
    preload = a.capture / 'preload.txt'
    report['observed_markers'] = len(observed)
    if preload.exists():
        pm = markers_from_preload(preload)
        report['preload_markers'] = len(pm)
        report['preload_sha256'] = hashlib.sha256(preload.read_bytes()).hexdigest()
    if a.reference:
        ref = [r['value'] for r in load(a.reference) if r.get('event') in ('marker', 'overlap-marker', 'target-marker')]
        refp = [v for v in ref if v.startswith('PATHTRACE ')]
        obsp = [v for v in observed if v.startswith('PATHTRACE ')]
        report['reference_marker_equal'] = refp == obsp
        report['reference_markers'] = len(refp)
        report['reference_first_difference'] = next(([i, x, y] for i, (x, y) in enumerate(zip(refp, obsp)) if x != y), None)
    if a.control:
        cp = a.control / 'preload.txt'
        if cp.exists() and preload.exists():
            report['control_preload_equal'] = markers_from_preload(cp) == markers_from_preload(preload)
            report['control_preload_sha256'] = hashlib.sha256(cp.read_bytes()).hexdigest()
    summary = dict(complete=report['complete'], lifo_violations=report['lifo_violations'],
                   fine_requests=len(fine), fine_restored=sum(r['restored'] for r in fine),
                   coarse_requests=len(coarse), coarse_order_ok=sum(r['call_order_ok'] for r in coarse),
                   coarse_restored_equal=sum(r['restored_equal'] for r in coarse),
                   coarse_cleared_at_search=sum(bool(r['cleared_at_search']) for r in coarse),
                   foreign_writers_inside_request_scopes=len(report['foreign_writers_inside_request_scopes']),
                   nesting=report['nesting'], writers_inside_scopes=report['writers_inside_scopes'],
                   reference_marker_equal=report.get('reference_marker_equal'),
                   control_preload_equal=report.get('control_preload_equal'),
                   terrain_edit_publication=report['terrain_edit_publication'])
    report['summary'] = summary
    a.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
