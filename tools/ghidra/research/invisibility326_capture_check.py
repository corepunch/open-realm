#!/usr/bin/env python3
"""Check original326 evidence; requires private DLL and archived capture files.

This verifies evidence integrity and the declared public projection, not engine
parity, additional repetitions, trajectories, or unobserved callback outcomes.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / 'tools/ghidra/fixtures/research'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
BASE = 0x6f000000
NAMES = ('invisibility326-effect-owner-original',
         'invisibility326-phase-validator-original',
         'invisibility326-timing-original', 'invisibility325-item-physical-original')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class PE:
    def __init__(self, data):
        self.data = data
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        require(data[pe:pe + 4] == b'PE\0\0', 'not PE')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        opt = pe + 24
        require(struct.unpack_from('<H', data, opt)[0] == 0x10b, 'not PE32')
        require(struct.unpack_from('<I', data, opt + 28)[0] == BASE, 'image base')
        table = opt + struct.unpack_from('<H', data, pe + 20)[0]
        self.sections = [struct.unpack_from('<IIII', data, table + i * 40 + 8)
                         for i in range(count)]

    def read(self, rva, length):
        for _virtual_size, va, size, offset in self.sections:
            if va <= rva and rva + length <= va + size:
                result = self.data[offset + rva - va:offset + rva - va + length]
                require(len(result) == length, 'truncated PE read')
                return result
        raise ValueError(f'unmapped RVA {rva:x}+{length:x}')


def check_bodies(pe):
    ranges, slots = {}, set()
    counts = {}
    for name in NAMES:
        doc = json.loads((FIXTURES / (name + '.json')).read_text())
        require(doc['original_sha256'] == SHA, name + ': original SHA')
        guards = doc.get('guards', doc.get('function_guards'))
        exports = doc.get('functions', doc.get('exports'))
        require(set(guards) == set(exports), name + ': unguarded export')
        for address, guard in guards.items():
            rva = int(guard['rva'], 16)
            body = bytes.fromhex(guard['bytes'])
            require(digest(body) == guard['sha256'], address + ': stored body SHA')
            require(pe.read(rva, len(body)) == body, address + ': original body')
            export = exports[address]
            require(int(export['body_start'], 16) == BASE + rva and
                    int(export['body_end'], 16) + 1 == BASE + rva + len(body),
                    address + ': complete body bounds')
            ranges[rva] = len(body)
        table = doc.get('slots', doc.get('vtable_slots', []))
        if isinstance(table, dict):
            table = list(table.values())
        if 'ai_vi_v394' in doc:
            table = [*table, doc['ai_vi_v394']]
        for slot in table:
            rva = int(slot.get('word_rva', slot.get('slot_rva')), 16)
            expected = struct.pack('<I', BASE + int(slot['target_rva'], 16))
            if 'bytes' in slot:
                require(bytes.fromhex(slot['bytes']) == expected, name + ': slot bytes')
            require(pe.read(rva, 4) == expected, name + ': original vtable slot')
            slots.add(rva)
        if 'getter202820_raw_bytes' in doc:
            body = bytes.fromhex(doc['getter202820_raw_bytes'])
            require(pe.read(0x202820, len(body)) == body, 'raw getter202820')
            ranges[0x202820] = len(body)
        counts[name] = len(guards)
    return counts, ranges, slots


def preload(path):
    return re.findall(r'call Preload\( "(I326 [^"\r\n]*)" \)', path.read_text())


def jsonlines(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def check_capture(root):
    doc = json.loads((FIXTURES / 'invisibility326-public-original.json').read_text())
    require(doc['accepted'] and doc['observations'] == 1 and
            doc['fresh_unhooked_controls'] == 1, 'capture cardinality')
    for name, sha in doc['files'].items():
        require(digest((root / name).read_bytes()) == sha, 'archive SHA: ' + name)
    base = root / 'build/tests/invisibility326'
    observed = jsonlines(base / 'observe.jsonl')
    control = jsonlines(base / 'control.jsonl')
    for rows, mode in ((observed, 'observe'), (control, 'control')):
        metadata = [r for r in rows if r.get('event') == 'metadata']
        require(len(metadata) == 1 and metadata[0]['mode'] == mode and
                metadata[0]['sha256'] == SHA, mode + ': metadata')
        end = [r for r in rows if r.get('event') == 'source-end']
        require(len(end) == 1 and end[0]['unchanged'] and
                end[0]['source_sha256'] == metadata[0]['source_sha256'], mode + ': source freeze')
        complete = [r for r in rows if r.get('event') == 'preload-file']
        require(len(complete) == 1 and complete[0]['complete'] and
                complete[0]['markers'] == 237, mode + ': completed preload')
    footers = [r for r in observed if r.get('event') == 'trace-end']
    require(len(footers) == 1 and footers[0] == doc['footer'], 'original trace footer')
    f = footers[0]
    require(f['complete'] and f['installed'] and f['readOnly'] and
            not f['dropped'] and not f['overflow'] and not f['pending_queries'], 'lossless footer')
    scoped = [r for r in observed if 'seq' in r]
    require([r['seq'] for r in scoped] == list(range(1, f['sequence'] + 1)), 'scoped sequence')
    require(len(scoped) == doc['scoped_records'] == 457, 'scoped count')
    require(dict(Counter(r['event'] for r in scoped)) == f['counts'], 'footer event counts')
    require(scoped == doc['private_records'], 'complete embedded scoped records')
    markers = [r['value'] for r in scoped if r['event'] == 'marker']
    # The startup transport emits start once into the start file, then once
    # into the complete public file. Remove exactly that authenticated prefix.
    require(markers[:2] == [doc['public'][0]] * 2, 'duplicated transport start')
    require(markers[1:] == doc['public'], 'hooked public order')
    public = preload(base / 'observe-preload.txt')
    require(public == preload(base / 'control-preload.txt') == doc['public'], 'public/control equality')
    require(len(public) == doc['public_records'] == 237, 'full public237')
    require(digest(('\n'.join(public) + '\n').encode()) == doc['public_sha256'], 'public SHA')
    require(public[-1] == 'I326 tick=60 scene=5 label=complete', 'completion marker')
    expected_events = ([289, 290, 291, 292, 293, 87], [289, 293, 290, 291],
                       [289, 290, 293, 291], [289, 290, 291, 293], [289, 293, 290, 291])
    lengths = []
    for n, scene in enumerate(doc['scenes']):
        rows = [r for r in public if re.search(rf' scene={n} ', r)]
        require(scene['scene'] == n and rows == scene['public_rows'], 'scene partition')
        first = next(i for i, r in enumerate(rows) if 'label=before-use' in r)
        last = next(i for i, r in enumerate(rows) if 'label=after-use' in r) + 1
        while 'label=state' not in rows[last]:
            last += 1
        projection = [r.split('label=', 1)[1] for r in rows[first:last + 1]
                      if 'label=pose' not in r]
        events = [int(re.search(r'id=(\d+)', r)[1]) for r in projection if r.startswith('event ')]
        require(events == list(expected_events[n]) == scene['event_ids'], 'phase order')
        require(projection[-3:-1] == ['use result=1', 'after-use'], 'accepted use returns')
        require(rows[last] == scene['after_use_state'], 'after-use state')
        require(projection.count('event id=87 code=0 used=1885957750') == (n == 0), 'USE_ITEM isolation')
        require(sum(r.startswith('event id=292 ') for r in projection) == (n == 0), 'FINISH isolation')
        if n == 4:
            require('event id=290 code=0 used=0' in projection and
                    'event id=291 code=0 used=0' in projection, 'detached source phase identity')
        lengths.append(len(projection))
    require(lengths == [18, 16, 16, 16, 15] and sum(lengths) == 81, 'declared projection81')
    return lengths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--capture-root', type=Path, default=ROOT,
                        help='root containing archived build/tests/invisibility326 files')
    args = parser.parse_args()
    data = args.binary.read_bytes()
    require(digest(data) == SHA, 'original DLL SHA256')
    counts, ranges, slots = check_bodies(PE(data))
    projection = check_capture(args.capture_root)
    print(json.dumps({'result': 'PASS', 'original_sha256': SHA,
                      'guard_counts': counts, 'unique_guarded_ranges':
                      [f'{rva:06x}..{rva + length - 1:06x}' for rva, length in sorted(ranges.items())],
                      'unique_vtable_slots': [f'{rva:06x}' for rva in sorted(slots)],
                      'public_records': 237, 'scoped_records': 457, 'projection': projection,
                      'limits': ['one observation plus one fresh unhooked control',
                                 'no engine comparison', 'projection excludes pose and later idle samples',
                                 '325 physical fixture checked for original bytes only']}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, StopIteration, struct.error) as error:
        raise SystemExit(f'FAIL: {error}')
