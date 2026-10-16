"""Complete original cancellation boundaries; no engine-generated expectations."""
import json
import gzip
from pathlib import Path
from collections import Counter


def extract(path):
    path = Path(path)
    raw = gzip.decompress(path.read_bytes()).decode() if path.suffix == '.gz' else path.read_text()
    rows = [json.loads(line) for line in raw.splitlines()]
    actors = [r for r in rows if r['event'] == 'actor']
    if [a['actor'] for a in actors] != list(range(96)) or len({a['path'] for a in actors}) != 96:
        raise ValueError('missing public wave bindings')
    markers = [r for r in rows if r['event'] == 'marker']
    labels = {r['value'].split(' label=')[1].split()[0]: r for r in markers if ' label=sample ' not in r['value']}
    if ([r['tick'] for r in markers if ' label=sample ' in r['value']] != list(range(1,46)) or
        list(labels) != ['start','wave_before','wave_after','pending_before','pending_stopped',
                         'pending_replaced','pending_next','travel_before','travel_stopped',
                         'travel_replaced','travel_next','complete']):
        raise ValueError('incomplete cancellation timeline')
    for marker in markers:
        if marker['searchDepth'] != 0:
            raise ValueError('public command interleaved a pure search')
        for bucket in marker['buckets']:
            q = bucket['queue']
            if (len(q) != len(set(q)) or bucket['head'] != (q[0] if q else '0x0') or
                bucket['tail'] != (q[-1] if q else '0x0')):
                raise ValueError('corrupt survivor FIFO')
    for before, after, actor in [('pending_before','pending_stopped',52),
                                 ('pending_stopped','pending_replaced',50)]:
        a, b = labels[before]['buckets'], labels[after]['buckets']
        path = actors[actor]['path']
        if a[3]['head'] != path:
            raise ValueError('pending victim was not the fine head')
        for old, new in zip(a,b):
            if (new['queue'] != [p for p in old['queue'] if p != path] or
                any(old[k] != new[k] for k in ('work','countdown','counter','clock'))):
                raise ValueError('cancellation changed survivors or accounting')
    def actor_at(label, index):
        return next(a for a in labels[label]['actors'] if a['actor'] == index)
    for label, index, replaced in [('pending_stopped',52,False),('pending_replaced',50,True),
                                    ('travel_stopped',94,False),('travel_replaced',95,True)]:
        a = actor_at(label,index)
        if (a['counts'] != [0,0] or a['indices'] != [0xffffffff,0xffffffff] or
            a['links'] != [0,0] or a['velocity'] != [0,0] or not a['flags'] & 0x100000 or
            not a['originalGroupLive'] or not a['originalGroupPathLive'] or
            a['orderCount'] != int(replaced)):
            raise ValueError('native return lost or retained the wrong owner')
        if not replaced and a['groupIdentity'] != [0xffffffff,0xffffffff]:
            raise ValueError('Stop kept physical ownership')
    for label, indices in [('pending_next',(50,52)),('travel_next',(94,95))]:
        for index in indices:
            a = actor_at(label,index)
            if a['originalGroupLive'] or a['originalGroupPathLive']:
                raise ValueError('old owner leaked beyond the following update')
    if any(b['queue'] for b in labels['complete']['buckets']):
        raise ValueError('final Stop kept queued work')
    depth, searched, stops = 0, Counter(), []
    for row in rows:
        event = row['event']
        if event == 'search-begin':
            depth += 1
            if row['depth'] != depth or depth != 1:
                raise ValueError('nested/missing search return')
            searched[row['kind']] += 1
        elif event == 'search-end':
            if row['depth'] != depth or depth != 1:
                raise ValueError('unpaired search')
            depth -= 1
        elif event in ('stop-begin','stop-end'):
            if row['searchDepth'] != 0 or depth:
                raise ValueError('Stop interrupted a pure original search')
            stops.append(row)
    if depth or sum(searched.values()) != 737 or len(stops) != 984:
        raise ValueError('incomplete search/Stop stream')
    # Original deterministic heap addresses happen to repeat exactly here. This
    # evidence check does not require the engine to share those private pointers.
    stream = [r for r in rows if r['event'] not in ('metadata','loading-key','trace-end','preload-file')]
    return dict(stream=stream, searches=dict(searched), stops=len(stops)//2, public_markers=len(markers))
