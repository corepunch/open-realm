#!/usr/bin/env python3
"""Analyze SEP-03/MAP-05/MAP-06 research captures (sep03_map06_trace.py output).

  spatial <capture.jsonl>                 SEP-03.x / MAP-05.x live summary
  markers <a-preload.txt> <b-preload.txt> exact JASS marker comparison (observer control)
  lifetime <capture.jsonl>                MAP-06.1 restart / change-level summary
  save <capture.jsonl> [control.jsonl]    MAP-06.2 save / load summary
Prints JSON; never edits captures.
"""
import collections
import hashlib
import json
import re
import struct
import sys
from pathlib import Path


def rows_of(path):
    out = []
    for line in Path(path).read_text().splitlines():
        if line.strip():
            out.append(json.loads(line))
    return out


def fval(word):
    return struct.unpack('<f', struct.pack('<I', int(word, 16)))[0]


def summary_meta(rows, path):
    meta = next(r for r in rows if r.get('event') == 'metadata')
    errors = [r for r in rows if r.get('type') == 'error' or r.get('event') in ('trace-failed', 'snapshot-error')]
    end = next((r for r in rows if r.get('event') in ('trace-end', 'control-end')), None)
    art = next((r for r in rows if r.get('event') == 'artifacts'), None)
    return dict(capture=str(path), capture_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(), map=meta['map'],
                mode=meta.get('mode'), source_sha256=meta.get('source_sha256'), events=len(rows), errors=errors[:5],
                complete=end is not None, caps=(end or {}).get('caps'), artifacts=art)


def spatial(path):
    rows = rows_of(path)
    out = summary_meta(rows, path)
    markers = [r['value'] for r in rows if r.get('event') == 'marker']
    out['markers'] = len(markers)
    out['complete_marker'] = any('label=complete' in m for m in markers)
    snaps = {r['label']: r for r in rows if r.get('event') == 'snapshot'}
    def named_chains(snap, kind):
        res = {}
        for ch in (snap.get(kind) or {}).get('chains', []):
            names = [x[3] for x in ch['chain'] if len(x) > 3]
            if any(n and n[0] in 'AB' and n[1] == '.' for n in names):
                res[ch['cell']] = [[x[0], x[1], x[3] if len(x) > 3 else 'meta'] for x in ch['chain']]
        return res
    if 'snapshot_inserted' in snaps:
        s = snaps['snapshot_inserted']
        out['inserted'] = dict(proximity=named_chains(s, 'proximity'), fine=named_chains(s, 'fine'),
                               objects=[{k: o[k] for k in ('name', 'rect', 'refs', 'stamp')} for o in s['objects']])
    comp = [r for r in rows if r.get('event') == 'compact-dirty']
    cad = {}
    for kind in ('proximity', 'fine'):
        ks = [r for r in comp if r['kind'] == kind]
        if not ks:
            continue
        clocks = [fval(r['clock']) for r in ks]
        gaps = collections.Counter(round(b - a, 4) for a, b in zip(clocks, clocks[1:]))
        stamps = [int(r['before']['stamp'], 16) for r in ks]
        cad[kind] = dict(callbacks=len(ks), first_clock_words=[r['clock'] for r in ks[:4]], gaps=dict(gaps.most_common(4)),
                         clock_span=[clocks[0], clocks[-1]], callers=sorted({r['caller'] for r in ks}),
                         stamp_first_last=[hex(stamps[0]), hex(stamps[-1])],
                         stamp_rate_per_clock_unit=(stamps[-1] - stamps[0]) / (clocks[-1] - clocks[0]),
                         max_metadata_links_between=max(r['metadataLinksSinceLast'] for r in ks),
                         total_metadata_links=sum(r['metadataLinksSinceLast'] for r in ks),
                         max_records=max(r['before']['records'] for r in ks),
                         max_link_high_water=max(r['before']['linkCount'] for r in ks),
                         capacity=sorted({r['before']['capacity'] for r in ks}), growth=sorted({r['before']['growth'] for r in ks}),
                         with_dirty=sum(1 for r in ks if r['before']['dirty']),
                         reset_seen=any(int(r['after']['stamp'], 16) < int(r['before']['stamp'], 16) for r in ks))
        # Per-window stamp rate (burst of 40 moving units starts at JASS tick 60).
        win = [r for r in ks if 62 <= r['tick'] <= 88]
        if len(win) > 1:
            cad[kind]['burst_stamp_rate_per_clock_unit'] = ((int(win[-1]['before']['stamp'], 16) - int(win[0]['before']['stamp'], 16)) /
                                                            (fval(win[-1]['clock']) - fval(win[0]['clock'])))
        idle = [r for r in ks if 25 <= r['tick'] <= 45]
        if len(idle) > 1:
            cad[kind]['two_mover_stamp_rate_per_clock_unit'] = ((int(idle[-1]['before']['stamp'], 16) - int(idle[0]['before']['stamp'], 16)) /
                                                                (fval(idle[-1]['clock']) - fval(idle[0]['clock'])))
    same_deadline_order = []
    for a, b in zip(comp, comp[1:]):
        if a['clock'] == b['clock'] and a['kind'] != b['kind']:
            same_deadline_order.append((a['kind'], b['kind']))
    cad['order_at_same_deadline'] = dict(collections.Counter(f'{a}->{b}' for a, b in same_deadline_order))
    out['cadence'] = cad
    # Removal -> reclamation timing (event order in the stream).
    removals = []
    for i, r in enumerate(rows):
        if r.get('event') == 'sobj-retire' and r.get('name'):
            rec = next((j for j in range(i, len(rows)) if rows[j].get('event') == 'sobj-recycle' and rows[j]['obj'] == r['obj']), None)
            nxt = next((j for j in range(i, len(rows)) if rows[j].get('event') == 'compact-dirty' and rows[j]['kind'] == r['kind']), None)
            removals.append(dict(name=r['name'], tick=r['tick'], kind=r['kind'], rect=r['rect'], refs_word=f"{r['refs']:08x}",
                                 recycled_before_next_same_map_compaction_end=rec is not None and nxt is not None and rec < nxt,
                                 compaction_clock=rows[nxt]['clock'] if nxt else None,
                                 recycle_refs=rows[rec]['refs'] if rec else None))
    out['named_removals'] = removals
    out['link_reserve'] = [dict(kind=r['kind'], bytes=r['bytes'], before=r['before'], after=r['after'], tick=r['tick'],
                                backtrace=r['backtrace']) for r in rows if r.get('event') == 'link-reserve']
    out['pool_blocks'] = [dict(tick=r['tick'], raw=r['raw'], caller=r['caller']) for r in rows if r.get('event') == 'spool-block']
    # Recycle -> reuse (LIFO) across the burst removal (tick 90) and reuse (tick 100) windows.
    recycled_90 = [r['obj'] for r in rows if r.get('event') == 'sobj-recycle' and 90 <= r['tick'] < 100]
    allocs_100 = [r for r in rows if r.get('event') == 'spool-alloc' and r['tick'] == 100]
    stack = list(recycled_90)
    expected = []
    for _ in allocs_100:
        expected.append(stack.pop() if stack else 'raw')
    got = [int(r['element'], 16) for r in allocs_100]
    want = [int(x, 16) if x != 'raw' else None for x in expected]
    out['pool_reuse'] = dict(recycled_in_window=len(recycled_90), allocations=len(allocs_100),
                             from_recycled=sum(1 for r in allocs_100 if r['fromRecycled']),
                             lifo_prediction_holds=(got == want) if recycled_90 else None)
    if 'complete' in snaps:
        c = snaps['complete']
        out['complete_snapshot'] = dict(proximity_state=c['proximity']['state'], fine_state=c['fine']['state'], pool=c['pool'])
    upd = [r for r in rows if r.get('event') == 'sobj-update']
    out['updates_named'] = len(upd)
    out['free_list_reuse_updates'] = sum(1 for r in upd if r['after']['linkCount'] == r['before']['linkCount'] and r['after']['records'] > r['before']['records'])
    return out


def lifetime(path):
    """MAP-06.1: phases separated by maps-create; per phase owner/maps/pools, teardown order and M trajectory."""
    rows = rows_of(path)
    out = summary_meta(rows, path)
    phases, cur = [], None
    for i, r in enumerate(rows):
        e = r.get('event')
        if e == 'maps-create':
            cur = dict(index=i, owner=r['owner'], maps=r['after'], spatial=[{k: m[k] for k in ('map', 'dims', 'cellData', 'linkData', 'capacity', 'records', 'stamp', 'timer')} for m in r['spatial']],
                       map_pool=r['mapPool'], movers=[], commits=collections.defaultdict(list), retire_callers=collections.Counter(),
                       teardown=None, markers=[])
            phases.append(cur)
        if cur is None:
            continue
        if e == 'mover-activate' and r.get('name') == 'M':
            cur['movers'].append(r['mover'])
        if e == 'mover-commit' and r['mover'] in cur['movers']:
            cur['commits'][r['mover']].append([r['tick'], r['pos'], r['vel']])
        if e == 'mover-retire':
            cur['retire_callers'][r['caller']] += 1
            cur.setdefault('first_retire_index', i)
        if e == 'marker' and 'label=sample' not in r['value']:
            cur['markers'].append(r['value'])
        if e == 'maps-release-begin' and r['maps'][0] != '0x0':
            rel = next((x for x in rows[i:] if x.get('event') == 'maps-release-end'), None)
            smr = [x for x in rows[i:i + 2000] if x.get('event') == 'spatial-map-release'][:2]
            dest = next((x for x in rows[i:] if x.get('event') == 'owner-destroy'), None)
            cur['teardown'] = dict(index=i, retires_before=cur['retire_callers'].copy(), spatial_before=r['spatial'],
                                   spatial_pool_live_before=r['spatialPool']['live'],
                                   spatial_map_release=[dict(kind=x['before']['kind'], before_records=x['before']['records'], after=x['after']) for x in smr],
                                   release_end=rel and dict(maps=rel['maps'], mapPool=rel['mapPool'], spatialPool=rel['spatialPool']),
                                   owner_destroy=dest and dict(owner=dest['owner'], spatialPool=dest['spatialPool']))
    res = []
    for ph in phases:
        commits = {m: c for m, c in ph['commits'].items()}
        res.append(dict(owner=ph['owner'], maps=ph['maps'], spatial=ph['spatial'], map_pool=ph['map_pool'], movers=ph['movers'],
                        commit_counts={m: len(c) for m, c in commits.items()}, retire_callers=dict(ph['retire_callers']),
                        teardown=ph['teardown'], nonsample_markers=ph['markers'][:12]))
    # First-movement comparison: every M commit sequence against the first phase's first M.
    seqs = [c for ph in phases for m, c in ph['commits'].items()]
    cmp = []
    if seqs:
        ref = seqs[0]
        for k, s in enumerate(seqs[1:], 1):
            n = min(len(ref), len(s))
            eq = next((j for j in range(n) if ref[j][1:] != s[j][1:]), n)
            cmp.append(dict(sequence=k, compared=n, equal_prefix=eq, ref_len=len(ref), len=len(s)))
    out['phases'] = res
    out['first_movement_vs_first_phase'] = cmp
    return out


def save(path):
    """MAP-06.2: save/load event order, spatial chains around the crowd, per-tick unit positions."""
    rows = rows_of(path)
    out = summary_meta(rows, path)
    order = []
    for i, r in enumerate(rows):
        e = r.get('event')
        if e in ('owner-save-begin', 'owner-save-end', 'maps-save-begin', 'maps-save-end', 'owner-load-begin', 'owner-load-end',
                 'compact-all', 'map-save', 'map-load', 'maps-create', 'maps-release-begin', 'owner-create', 'owner-destroy') or \
                (e == 'marker' and any(k in r['value'] for k in ('before_save', 'after_save', 'load_armed', 'before_load', 'after_load_call', 'resumed_after_load'))):
            row = dict(i=i, event=e, tick=r.get('tick'))
            if e in ('map-save', 'map-load'):
                row.update(kind=r['after']['kind'], before={k: r['before'][k] for k in ('records', 'linkCount', 'freeHead', 'stamp', 'cellData', 'linkData')},
                           after={k: r['after'][k] for k in ('records', 'linkCount', 'freeHead', 'stamp', 'cellData', 'linkData')})
            if e == 'compact-all':
                row.update(kind=r['kind'], records=[r['before']['records'], r['after']['records']], dirty=[r['before']['dirty'], r['after']['dirty']])
            if e == 'marker':
                row['value'] = r['value'][:90]
            if e == 'maps-create':
                row['maps'] = r['after'][:2]
            order.append(row)
    out['order'] = order
    saves = [r for r in rows if r.get('event') == 'sobj-save']
    loads = [r for r in rows if r.get('event') == 'sobj-load']
    out['object_saves'] = dict(count=len(saves), kinds={f'{a}/{b}': n for (a, b), n in collections.Counter((r['kind'], r['flags']) for r in saves).items()},
                               named=[{k: r[k] for k in ('name', 'kind', 'rect', 'refs', 'stamp', 'flags')} for r in saves if r.get('name')])
    out['object_loads'] = dict(count=len(loads), kinds={f'{a}/{b}': n for (a, b), n in collections.Counter((r['kind'], r['flags']) for r in loads).items()},
                               first_index=next((i for i, r in enumerate(rows) if r.get('event') == 'sobj-load'), None),
                               map_load_indices=[i for i, r in enumerate(rows) if r.get('event') == 'map-load'],
                               sample=[{k: r[k] for k in ('obj', 'kind', 'rect', 'refs', 'stamp', 'flags', 'mapRecords')} for r in loads[:6]])
    snaps = [r for r in rows if r.get('event') == 'snapshot']
    def crowd_cells(snap):
        res = {}
        for ch in (snap.get('proximity') or {}).get('chains', []):
            if len(ch['chain']) > 1:
                res[ch['cell']] = [[x[0], x[1], x[3] if len(x) > 3 else 'meta', x[2]] for x in ch['chain']]
        return res
    out['snapshots'] = [dict(label=s['label'], tick=s.get('tick'), proximity_multi=crowd_cells(s),
                             proximity_state=(s.get('proximity') or {}).get('state'), pool=s.get('pool')) for s in snaps]
    return out


def markers_of(path):
    text = Path(path).read_bytes().decode('utf-8', 'replace')
    return re.findall(r'call Preload\( "((?:RSPATIAL|RSCROWD)[^"\r\n]*)" \)', text)


def compare_markers(a, b):
    ma, mb = markers_of(a), markers_of(b)
    diffs = [dict(index=i, a=x, b=y) for i, (x, y) in enumerate(zip(ma, mb)) if x != y]
    return dict(a=str(a), b=str(b), a_sha256=hashlib.sha256(Path(a).read_bytes()).hexdigest(),
                b_sha256=hashlib.sha256(Path(b).read_bytes()).hexdigest(), a_markers=len(ma), b_markers=len(mb),
                equal=ma == mb, differences=diffs[:20], difference_count=len(diffs) + abs(len(ma) - len(mb)))


def main():
    mode = sys.argv[1]
    if mode == 'spatial':
        print(json.dumps(spatial(sys.argv[2]), indent=1))
    elif mode == 'lifetime':
        print(json.dumps(lifetime(sys.argv[2]), indent=1))
    elif mode == 'save':
        print(json.dumps(save(sys.argv[2]), indent=1))
    elif mode == 'markers':
        print(json.dumps(compare_markers(sys.argv[2], sys.argv[3]), indent=1))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
