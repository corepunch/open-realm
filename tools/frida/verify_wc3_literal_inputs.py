#!/usr/bin/env python3
"""Strict compiled-token observer and bracketed public-native repeat contract."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_wc3_numeric_inputs import verify as verify_numeric, numeric_sequence

FIXTURE = Path(__file__).parents[1] / 'ghidra/fixtures/retail-compiled-literal-inputs-1.27.json'


def literal_sequence(rows):
    return [{k: v for k, v in row.items() if k != 'ms'} for row in rows if row.get('event') == 'numeric-literal']


def verify(rows, fixture):
    report = verify_numeric(rows, fixture)
    observed = literal_sequence(rows)
    if observed != fixture['literal_sequence']:
        report['violations'].append('compiled literal words, token codes, caller sites or event order differ')
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if ends and ends[0].get('counts', {}).get('numeric-literal') != len(observed):
        report['violations'].append('compiled literal observer count differs')
    report['compiled_events'] = len(observed)
    report['literal_sha256'] = hashlib.sha256(json.dumps(observed, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--repeat', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    first, second = [[json.loads(line) for line in path.read_text().splitlines()] for path in (args.capture, args.repeat)]
    report = verify(first, fixture)
    other = verify(second, fixture)
    report['violations'] += ['repeat: ' + e for e in other['violations']]
    report['repeat_equal'] = numeric_sequence(first) == numeric_sequence(second) and literal_sequence(first) == literal_sequence(second)
    if not report['repeat_equal']:
        report['violations'].append('compiler or public native repeat differs')
    report['scope'] = fixture['scope']
    report['captures'] = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.capture, args.repeat)]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['violations']))


if __name__ == '__main__':
    main()
