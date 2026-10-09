#!/usr/bin/env python3
"""SEP-02.2/02.3/04.x: replay every live Separate_Update body with original code and compare words.

For each completed sep-update row of a sep_research_observer capture (with --pair-probes):
  * per neighbor k (enumeration order of 6f16f570): original pair slice on the live inputs captured at
    6f1703e0 (source/candidate positions, retained vector, owner PRNG words) -> must equal the live words
    captured at 6f170518 (vector, owner PRNG state);
  * original tail on the live accumulated vector and packed word -> must equal the live leave state;
  * the retained-vector application attempt (6f16ffa0) is reported (endpoint, validator result,
    position change) and the first pair input vector must equal the pre-visit vector (no clearing).
Mismatches are preserved in the report, never filtered.
"""
import argparse, hashlib, json, math, sys
from collections import Counter
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'frida/research'))
from sep_research_oracle import Oracle  # noqa: E402
from sep_research_analyze import Capture, fw, decode_word  # noqa: E402


def vlen(w):
    return math.hypot(fw(w[0]), fw(w[1]))


def replay(cap, o, keep_rows=True):
    out, stats = [], Counter()
    for v in cap.updates:
        before_word = v['before']['word']
        row = dict(i=v.get('_i'), tick=v['tick'], visit=v['visit'], sep=v['sep'], wordBefore=before_word,
                   vecBefore=v['before']['vec'], vecAfter=v['after']['vec'], wordAfter=v['after']['word'],
                   posBefore=v['moverBefore']['pos'], posAfter=v['moverAfter']['pos'],
                   occBefore=v['moverBefore']['occ'], occAfter=v['moverAfter']['occ'], speed=v['moverBefore']['speed'])
        if before_word & 0xffff:
            row['kind'] = 'cooldown'
            stats['cooldown'] += 1
            ok = v['after']['word'] == before_word - 1 and v['after']['vec'] == v['before']['vec']
            row['match'] = ok; stats['cooldown_ok' if ok else 'cooldown_mismatch'] += 1
            if keep_rows: out.append(row)
            continue
        if v['moverBefore']['speed'] not in (0, 0x80000000):
            row['kind'] = 'moving'
            stats['moving'] += 1
            ok = v['after']['vec'] == [0, 0] and v['after']['word'] == (before_word & 0xffff0000) | 7
            row['match'] = ok; stats['moving_ok' if ok else 'moving_mismatch'] += 1
            if keep_rows: out.append(row)
            continue
        row['kind'] = 'body'; stats['body'] += 1
        sel = (before_word >> 16) & 15
        settings = o.settings[sel]
        ap = v['apply']
        row['apply'] = dict(vec=ap['vec'], endpoint=ap.get('endpoint'), valid=ap.get('valid'), pos=ap['pos'], posAfter=ap['posAfter'],
                            moved=ap['pos'] != ap['posAfter'], occ=ap.get('occ'), occAfter=ap.get('occAfter'))
        members = v['query']['members'] if v['query'] else []
        pairs = v['pairs']
        steps, ok = [], True
        if len(pairs) != len(members):
            ok = False; row['error'] = f'pair probes {len(pairs)} != candidates {len(members)}'
        if pairs and pairs[0]['vecBefore'] != v['before']['vec']:
            ok = False; row['error'] = 'retained vector changed before accumulation'
        vec = v['before']['vec']
        for k, p in enumerate(pairs):
            st_after, vec_after, drew = o.pair(settings, p['ownerBefore'], p['source'], p['candidate'], p['vecBefore'])
            m = vec_after == p.get('vecAfter') and st_after == p.get('ownerAfter') and p['vecBefore'] == vec
            ok &= m
            delta = (fw(vec_after[0]) - fw(p['vecBefore'][0]), fw(vec_after[1]) - fw(p['vecBefore'][1]))
            dx, dy = fw(p['source'][0]) - fw(p['candidate'][0]), fw(p['source'][1]) - fw(p['candidate'][1])
            steps.append(dict(k=k, j=members[k].get('_j') if k < len(members) else None, source=p['source'], candidate=p['candidate'],
                              distanceInput=math.hypot(dx, dy), distanceWord=p.get('distance'), randomBranch=drew,
                              ownerBefore=p['ownerBefore'], ownerAfter=p.get('ownerAfter'), vecBefore=p['vecBefore'],
                              vecAfterLive=p.get('vecAfter'), vecAfterOracle=vec_after, contribution=delta, match=m))
            vec = p.get('vecAfter', vec_after)
            stats['pairs'] += 1; stats['pairs_ok' if m else 'pairs_mismatch'] += 1
            if drew: stats['random_pairs'] += 1
        final, packed = o.tail(settings, vec, before_word)
        tm = final == v['after']['vec'] and packed == v['after']['word']
        ok &= tm
        stats['tails_ok' if tm else 'tails_mismatch'] += 1
        row.update(selector=sel, settings=settings, steps=steps, accumulated=vec, accumulatedLength=vlen(vec),
                   damped=vlen(vec) * fw(settings[4]), tailOracle=dict(vec=final, word=packed), tailMatch=tm,
                   outcome='deadzone+cooldown7' if packed & 0xffff == 7 and final == [0, 0] else ('capped' if abs(vlen(final) - fw(settings[2])) < 1e-6 else 'scaled'),
                   match=ok)
        stats['body_ok' if ok else 'body_mismatch'] += 1
        if keep_rows: out.append(row)
    return out, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    o = Oracle(a.binary)
    cap = Capture(a.capture, a.map_json)
    rows, stats = replay(cap, o)
    rep = dict(binary_sha256=o.sha, crt_sha256=o.crt_sha, capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(),
               complete=cap.complete, stats=dict(stats), fine_offset=cap.offset,
               scope='Original pair slices and tail executed on live words; query enumeration/filter/application are live observations.',
               rows=rows)
    a.report.write_text(json.dumps(rep) + '\n')
    print(json.dumps(dict(stats), indent=1))


if __name__ == '__main__':
    main()
