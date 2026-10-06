#!/usr/bin/env python3
"""Compare the exact PATHSEP JASS Preload marker sequences of two capture directories (observer vs control/repeat)."""
import argparse, hashlib, json, re
from pathlib import Path


def markers(d):
    raw = (Path(d) / 'preload.txt').read_bytes()
    return re.findall(r'call Preload\( "(PATHSEP[^"\r\n]*)" \)', raw.decode('utf-8', 'replace')), hashlib.sha256(raw).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('first', type=Path)
    ap.add_argument('second', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    (m1, h1), (m2, h2) = markers(a.first), markers(a.second)
    diffs = [dict(index=i, first=x, second=y) for i, (x, y) in enumerate(zip(m1, m2)) if x != y]
    res = dict(first=str(a.first), second=str(a.second), first_preload_sha256=h1, second_preload_sha256=h2,
               first_markers=len(m1), second_markers=len(m2), equal=m1 == m2, differences=diffs[:50], difference_count=len(diffs),
               complete=[bool(m1) and ' label=complete' in m1[-1], bool(m2) and ' label=complete' in m2[-1]],
               precision='exact JASS strings; positions R2SW(...,1,4), orders, owners per 0.1 s sample')
    a.out.write_text(json.dumps(res, indent=1) + '\n')
    print(json.dumps({k: res[k] for k in ('first_markers', 'second_markers', 'equal', 'difference_count', 'complete')}))


if __name__ == '__main__':
    main()
