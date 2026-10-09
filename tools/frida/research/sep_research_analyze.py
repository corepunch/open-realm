#!/usr/bin/env python3
"""Shared analysis for SEP research captures (sep_research_observer.js) and JASS Preload controls.

load(capture_dir, map_json) -> Capture with unit<->mover<->separation mapping by creation order
(first Unit_RefreshSeparationConfiguration from the unit-init site 6f68a4fa per new unit pointer,
cross-checked against the JASS roster rawcode/owner), world<->fine calibration and helpers.
"""
import json, re, struct
from collections import defaultdict
from pathlib import Path

INIT_REFRESH = 0x68a4fa


def fw(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]


def rawcode(code):
    return int.from_bytes(code.encode(), 'big')


def decode_word(w):
    return dict(cooldown=w & 0xffff, selector=(w >> 16) & 15, category=(w >> 20) & 0xff, owner_nibble=(w >> 24) & 15,
                group_nibble=(w >> 20) & 15, rank=w >> 28)


def filter_reason(f, source_mover):
    if f['obj34'] & 0xff000000:
        return 'object+34 high byte'
    if f['mover'] == source_mover:
        return 'self'
    if f.get('tag') != 0x60706375:
        return 'type tag'
    if f['flags14'] & 0x80000000:
        return 'mover+14 bit31'
    if fw(f['radius']) <= 0:
        return 'radius<=0'
    if f['speed'] not in (0, 0x80000000):
        return 'moving (c0!=0)'
    if f['word'] is None:
        return 'no separation (+ac null)'
    if (f['word'] >> 20) & 0xff != f['qcat']:
        return 'category mismatch'
    if f['word'] >> 28 < f['qrank']:
        return 'rank below source minimum'
    return 'eligible'


def preload_rows(text):
    return re.findall(r'call Preload\( "(PATHSEP[^"\r\n]*)" \)', text)


def parse_marker(v):
    d = dict(re.findall(r'(\w+)=(\S+)', v))
    return d


class Capture:
    def __init__(self, cdir, map_json):
        cdir = Path(cdir)
        self.dir = cdir
        self.meta = json.loads(Path(map_json).read_text())
        self.rows = [json.loads(l) for l in (cdir / 'capture.jsonl').open()] if (cdir / 'capture.jsonl').exists() else []
        self.prov = json.loads((cdir / 'provenance.json').read_text())
        self.preload = preload_rows((cdir / 'preload.txt').read_text('utf-8', 'replace')) if (cdir / 'preload.txt').exists() else []
        self.roster = self.meta['roster']
        self.complete = bool(self.preload) and ' label=complete' in self.preload[-1] and any(
            r.get('event') == 'trace-end' and r.get('installed') for r in self.rows) and not any(
            r.get('event') in ('trace-failed', 'script-error') for r in self.rows)
        # unit index <-> unit pointer <-> mover; heap pointers are reused after RemoveUnit, so the
        # mapping is maintained sequentially: each init refresh (6f68a4fa) opens the next roster index.
        unit_idx, mover_idx = {}, {}
        self.mover_of, self.configs, self.index_of_mover = {}, defaultdict(list), {}
        nxt = 0
        # 6f1710e0 runs nested inside 6f693d50 and is emitted first; link each configure row to the
        # enclosing refresh (the next refresh row for the same unit pointer).
        enclosing = {}
        pending = {}
        for k, r in enumerate(self.rows):
            if r['event'] == 'configure' and r['unit']:
                pending[r['unit']] = k
            elif r['event'] == 'refresh' and r['unit'] in pending:
                enclosing[pending.pop(r['unit'])] = r
        for k, r in enumerate(self.rows):
            ev = r['event']
            if ev == 'configure' and r['unit'] and enclosing.get(k, {}).get('caller') == INIT_REFRESH:
                ref = enclosing[k]
                if nxt >= len(self.roster):
                    raise ValueError('more unit creations than roster')
                ro = self.roster[nxt]
                assert ref['rawcode'] == rawcode(ro['code']) and ref['ownerIndex'] == ro['owner'], (nxt, ref, ro)
                unit_idx[r['unit']] = nxt; nxt += 1
            if ev == 'refresh':
                r['_i'] = unit_idx.get(r['unit'])
            elif ev == 'configure':
                i = unit_idx.get(r['unit']) if r['unit'] else mover_idx.get(r['mover'])
                r['_i'] = i
                if i is not None:
                    if r['unit']:
                        mover_idx[r['mover']] = i
                        self.mover_of.setdefault(i, r['mover'])
                    self.configs[i].append(r)
                if r['caller'] == 0x16eb5e:  # Mover_Destroy: mapping ends here
                    mover_idx.pop(r['mover'], None)
            elif ev == 'sep-update':
                r['_i'] = mover_idx.get(r['mover'])
                for f in r.get('filter') or []:
                    f['_j'] = mover_idx.get(f['mover'])
                if r.get('query'):
                    for m in r['query']['members']:
                        m['_j'] = mover_idx.get(m['mover'])
        if nxt != len(self.roster):
            raise ValueError(f'unit creation count {nxt} != roster {len(self.roster)}')
        self.updates = [r for r in self.rows if r['event'] == 'sep-update']
        self.markers = [r for r in self.rows if r['event'] == 'marker']
        # JASS samples (from Preload file, the observer-independent output)
        self.samples = defaultdict(list)
        for v in self.preload:
            d = parse_marker(v)
            if d.get('label') == 's':
                self.samples[int(d['i'])].append((int(d['tick']), float(d['x']), float(d['y']), int(d['o']), int(d['p'])))
        self._calibrate()

    def _calibrate(self):
        """fine = world/32 + offset per axis, from creation positions vs first stored mover position."""
        offs = []
        for r in self.updates:
            i = r.get('_i')
            if i is None:
                continue
            ro = self.roster[i]
            fx, fy = (fw(w) for w in r['moverBefore']['pos'])
            offs.append((round(fx - ro['x'] / 32, 3), round(fy - ro['y'] / 32, 3)))
            if len(offs) > 64:
                break
        from collections import Counter
        self.offset = Counter(offs).most_common(1)[0][0] if offs else (0, 0)

    def world(self, pos_words):
        x, y = (fw(w) for w in pos_words)
        return ((x - self.offset[0]) * 32, (y - self.offset[1]) * 32)

    def visits(self, i):
        return [r for r in self.updates if r.get('_i') == i]
