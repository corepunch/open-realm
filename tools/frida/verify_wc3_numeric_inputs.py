#!/usr/bin/env python3
"""Strict bracketed public-native/decimal-parser capture contract and repeat check."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

FIXTURE = Path(__file__).parents[1] / 'ghidra/fixtures/retail-public-numeric-inputs-1.27.json'


def numeric_sequence(rows):
    return [{k: v for k, v in row.items() if k != 'ms'} for row in rows
            if row.get('event') in ('numeric-marker', 'numeric-native', 'numeric-parser')]


def verify(rows, fixture):
    errors = []
    metadata = [r for r in rows if r.get('event') == 'metadata']
    if len(metadata) != 1 or metadata[0].get('sha256') != fixture['target_sha256'] or metadata[0].get('source_sha256') != fixture['source_sha256']:
        errors.append('target or capture-source provenance differs')
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(ends) != 1 or not ends[0].get('installed') or any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        errors.append('capture incomplete or observer failed')
    expected = []
    for case in fixture['cases']:
        identity, name = case['id'], case['native']
        expected.append(dict(event='numeric-marker', value=f'PATHNUM case={identity} native={name}'))
        if name == 'S2R' and case['input']:
            expected.append(dict(event='numeric-parser', case=identity, native=name,
                                 text=case['input'], output=case['output']))
        native = dict(event='numeric-native', case=identity, native=name, output=case['output'])
        if 'input_word' in case:
            native['input'] = case['input_word']
        expected.append(native)
        # R2S/I2S text is a completion witness, never the numeric oracle.
        expected.append(dict(event='numeric-marker', done=identity))
    actual = numeric_sequence(rows)
    normalized = []
    for row in actual:
        value = row.get('value', '')
        if row['event'] == 'numeric-marker' and value.startswith('PATHNUM done=') and ' value=' in value:
            normalized.append(dict(event='numeric-marker', done=value.split(' ', 2)[1][5:]))
        else:
            normalized.append(row)
    if normalized != expected:
        errors.append('bracketed parser/native sequence or exact words differ')
    native_count = sum(r.get('event') == 'numeric-native' for r in rows)
    parser_count = sum(r.get('event') == 'numeric-parser' for r in rows)
    # Observer counters are sparse: no parser call means no parser key.
    if ends and (ends[0].get('counts', {}).get('numeric-native') != native_count or ends[0].get('counts', {}).get('numeric-parser', 0) != parser_count):
        errors.append('observer numeric counts differ from recorded events')
    destinations = [[struct.unpack('<I', struct.pack('<f', value))[0] for value in r['destination']]
                    for r in rows if r.get('event') == 'point-task']
    if destinations != [fixture['move_destination_words']]:
        errors.append('public Move did not retain the exact parsed destination')
    markers = [r.get('value', '') for r in rows if r.get('event') == 'marker']
    if not any('label=order_accepted ' in v for v in markers) or not any('tick=300 label=complete ' in v for v in markers):
        errors.append('parsed Move admission or timer completion missing')
    return dict(violations=errors, native_cases=native_count, parser_cases=parser_count,
                numeric_sha256=hashlib.sha256(json.dumps(actual, sort_keys=True, separators=(',', ':')).encode()).hexdigest())


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
    report['violations'] += ['repeat: ' + error for error in other['violations']]
    report['repeat_equal'] = numeric_sequence(first) == numeric_sequence(second)
    if not report['repeat_equal']:
        report['violations'].append('public numeric repeat differs')
    report['scope'] = fixture['scope']
    report['captures'] = [hashlib.sha256(path.read_bytes()).hexdigest() for path in (args.capture, args.repeat)]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['violations']))


if __name__ == '__main__':
    main()
