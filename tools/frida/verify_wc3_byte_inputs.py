#!/usr/bin/env python3
"""Reject substituted byte producers or CRT locales; verify public S2R words and exact repeats."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_wc3_numeric_inputs import verify as verify_numeric

FIXTURE = Path(__file__).parents[1]/'ghidra/fixtures/retail-public-byte-inputs-1.27.json'


def source_bytes(case):
    value = case['input']
    return value['prefix'].encode('ascii')+bytes([value['byte']])+value['suffix'].encode('ascii')


def byte_sequence(rows):
    return [{k:v for k,v in row.items() if k != 'ms'} for row in rows
            if row.get('event') in ('crt-module','numeric-marker','numeric-native','numeric-byte-parser','numeric-digit')]


def expected_digits(case):
    source = source_bytes(case)
    if source.startswith((b'-', b'+')):
        source = source[1:]
    observed = []
    point = False
    for byte in source:
        if byte in (0, 9, 32, 59):
            break
        digit = 48 <= byte <= 57
        observed.append(dict(event='numeric-digit', case=case['id'], native='S2R',
                             input=byte-256 if byte >= 128 else byte,
                             locale_ever_changed=0, ctype_rva=0x1158,
                             table_word=132 if digit else 16 if byte == 46 else 0,
                             output=4 if digit else 0))
        if digit:
            continue
        if byte == 46 and not point:
            point = True
            continue
        break
    return observed


def verify(rows, fixture):
    numeric_fixture = dict(fixture, cases=[dict(case, input=source_bytes(case).decode('latin1')) for case in fixture['cases']])
    numeric_rows = []
    for row in rows:
        if row.get('event') == 'numeric-byte-parser':
            try:
                text = bytes.fromhex(row.get('text_hex', '')).decode('latin1')
            except ValueError:
                text = '<invalid byte hex>'
            numeric_rows.append({k:v for k,v in dict(row, event='numeric-parser', text=text).items() if k != 'text_hex'})
        else:
            numeric_rows.append(row)
    report = verify_numeric(numeric_rows, numeric_fixture)
    metadata = [r for r in rows if r.get('event') == 'metadata']
    if len(metadata) != 1 or metadata[0].get('crt') != fixture['crt_config'] or not metadata[0].get('byteEvents') or not metadata[0].get('numericEvents'):
        report['violations'].append('exact sibling CRT identity or byte observer configuration differs')
    modules = [{k:v for k,v in r.items() if k != 'ms'} for r in rows if r.get('event') == 'crt-module']
    if modules != [fixture['crt_module']]:
        report['violations'].append('original default CRT locale/table witness differs')
    digits = [{k:v for k,v in r.items() if k != 'ms'} for r in rows if r.get('event') == 'numeric-digit']
    expected = [digit for case in fixture['cases'] for digit in expected_digits(case)]
    if digits != expected:
        report['violations'].append('signed byte promotion, original digit mask, table words or classification order differs')
    bracketed = [fixture['crt_module']]
    for case in fixture['cases']:
        identity = case['id']
        bracketed.append(dict(event='numeric-marker', value=f'PATHNUM case={identity} native=S2R'))
        bracketed.extend(expected_digits(case))
        bracketed.append(dict(event='numeric-byte-parser', case=identity, native='S2R',
                              text_hex=source_bytes(case).hex(), output=case['output']))
        bracketed.append(dict(event='numeric-native', case=identity, native='S2R', output=case['output']))
        bracketed.append(dict(event='numeric-marker', done=identity))
    normalized = []
    for row in byte_sequence(rows):
        value = row.get('value', '')
        if row['event'] == 'numeric-marker' and value.startswith('PATHNUM done=') and ' value=' in value:
            normalized.append(dict(event='numeric-marker', done=value.split(' ', 2)[1][5:]))
        else:
            normalized.append(row)
    if normalized != bracketed:
        report['violations'].append('byte parser/classifier/native observations escaped their case brackets')
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if ends and ends[0].get('counts', {}).get('numeric-digit') != len(digits):
        report['violations'].append('CRT digit observer count differs')
    report['digit_cases'] = len(digits)
    report['byte_sha256'] = hashlib.sha256(json.dumps(byte_sequence(rows), sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--repeat', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    first, second = [[json.loads(line) for line in p.read_text().splitlines()] for p in (args.capture, args.repeat)]
    report = verify(first, fixture)
    other = verify(second, fixture)
    report['violations'] += ['repeat: '+e for e in other['violations']]
    report['repeat_equal'] = byte_sequence(first) == byte_sequence(second)
    if not report['repeat_equal']:
        report['violations'].append('public byte/CRT observer repeat differs')
    report['scope'] = fixture['scope']
    report['captures'] = [hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.capture,args.repeat)]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['violations']))


if __name__ == '__main__':
    main()
