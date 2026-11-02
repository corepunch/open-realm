#!/usr/bin/env python3
"""Compare the JASS Preload marker streams of observed and observer-free control runs.

Used by GROUP-03.4.6.2.1.2 (prefix RSG) and GROUP-03.4.7.3 (prefix RSH).  Markers carry public
R2S positions (three decimals), current order ids and public native results; an identical stream
shows the read-only observer did not perturb the public frame.  AI-thread markers are compared too.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path


def markers(path, prefix):
    text = path.read_bytes().decode('utf-8', 'replace')
    return re.findall(r'call Preload\( "(' + prefix + r' [^"\r\n]*)" \)', text)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prefix', required=True)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--other', type=Path, action='append', required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    ref = markers(args.reference, args.prefix)
    out = dict(reference=str(args.reference), reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),
               reference_markers=len(ref), comparisons=[])
    for path in args.other:
        other = markers(path, args.prefix)
        diffs = [dict(index=i, reference=a, other=b) for i, (a, b) in enumerate(zip(ref, other)) if a != b]
        out['comparisons'].append(dict(other=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), markers=len(other),
                                       equal=ref == other, length_equal=len(ref) == len(other), differences=diffs[:50],
                                       difference_count=len(diffs)))
    args.output.write_text(json.dumps(out, indent=1) + '\n')
    print(json.dumps([{k: c[k] for k in ('other', 'markers', 'equal', 'difference_count')} for c in out['comparisons']], indent=1))


if __name__ == '__main__':
    main()
