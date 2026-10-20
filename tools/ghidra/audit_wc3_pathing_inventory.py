#!/usr/bin/env python3
"""Check a closed static retail inventory against the corpus, docs, open tasks and DLL.

Inventory leaves (BASE-01.4, BASE-03.1, BASE-03.2, NUM-01.2) publish one fixture
each. Every enumerated original site must be owned by exactly one row or one
explicit exclusion. Covered rows cite evidence; partial/uncovered rows name one
remaining open task. Evidence references must resolve to a corpus oracle, a
Markdown heading or a closed backlog leaf, so a stale reference cannot pass.
Unreachable rows carry a static proof; a missing producer is never assumed.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json'
TODO = ROOT / 'docs/games/warcraft-3/retail-pathfinding-todo.md'
DOCS = ROOT / 'docs/games/warcraft-3'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
STATUSES = {'covered', 'partial', 'uncovered', 'unreachable'}
TASK = re.compile(r'^- \[( |x)\] \*\*([A-Z][A-Z0-9]*-\d+(?:\.\d+)+)\*\*', re.M)
ADDRESS = re.compile(r'^6f[0-9a-f]{6}$')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def slug(heading):
    text = re.sub(r'[`*_]', '', heading.strip().lower())
    return re.sub(r'\s', '-', re.sub(r'[^\w\- ]', '', text))


def anchors(path, cache={}):
    if path not in cache:
        cache[path] = {slug(m[1]) for m in re.finditer(r'^#{1,6}\s+(.*)$', path.read_text(), re.M)}
    return cache[path]


def check_reference(ref, oracles, closed):
    kind, _, value = ref.partition(':')
    if kind == 'oracle':
        require(value in oracles, 'unknown oracle evidence: ' + ref)
    elif kind == 'doc':
        name, _, anchor = value.partition('#')
        path = DOCS / name
        require(path.is_file() and '/' not in name, 'unknown evidence document: ' + ref)
        require(not anchor or anchor in anchors(path), 'unknown evidence heading: ' + ref)
    elif kind == 'todo':
        require(value in closed, 'evidence task is not closed: ' + ref)
    else:
        raise ValueError('unknown evidence reference kind: ' + ref)


def guard_bytes(data, guards):
    """Compare guarded instructions with the mapped PE sections of `data`."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    size = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    sections = [struct.unpack_from('<IIII', data, pe + 24 + size + i * 40 + 8) for i in range(count)]
    for address, expected in guards.items():
        rva, want = int(address, 16) - base, bytes.fromhex(expected)
        found = [data[raw + rva - at:raw + rva - at + len(want)] for vsize, at, rsize, raw in sections
                 if at <= rva < at + min(vsize, rsize)]
        require(found == [want], 'original instruction differs at ' + address)
    return len(guards)


def original_bytes(binary, guards):
    data = binary.read_bytes()
    require(hashlib.sha256(data).hexdigest() == SHA, 'retail executable differs')
    return guard_bytes(data, guards)


def validate(inventory, corpus, todo, binary=None):
    task = inventory.get('task')
    states = {m[2]: m[1] for m in TASK.finditer(todo)}
    require(inventory.get('version') == 1 and task in states, 'unsupported inventory task')
    require(inventory.get('game_sha256') == SHA, 'inventory binary differs')
    open_tasks = {k for k, v in states.items() if v == ' ' and k != task}
    closed = {k for k, v in states.items() if v == 'x'}
    oracles = {e['id'] for e in corpus['entries']}
    enumerated = {}
    for query in inventory['enumeration']:
        require(query['id'] not in enumerated and query['query'].strip(), 'invalid enumeration query')
        require(query['sites'] and all(ADDRESS.match(s) for s in query['sites']), 'invalid enumerated site')
        enumerated[query['id']] = set(query['sites'])
    sites = set().union(*enumerated.values())
    owned, ids, statuses, owners = {}, set(), Counter(), Counter()
    for row in inventory['rows']:
        name = row['id']
        require(name.startswith(inventory['prefix']) and name not in ids, 'invalid or duplicate row: ' + name)
        ids.add(name)
        require(row['producer'].strip() and row['rationale'].strip(), 'row lacks producer/rationale: ' + name)
        require(row['status'] in STATUSES, 'invalid status: ' + name)
        for ref in row['evidence']:
            check_reference(ref, oracles, closed)
        if row['status'] == 'covered':
            require(row['evidence'] and row['owner'] is None, 'covered row needs evidence and no owner: ' + name)
        elif row['status'] == 'unreachable':
            # Unreachability is a static claim: it needs its own guarded proof text.
            require(row['owner'] is None and row.get('proof', '').strip(), 'unproved unreachable row: ' + name)
        else:
            require(row['owner'] in open_tasks, 'owner is not a remaining task: ' + name)
            owners[row['owner']] += 1
        for site in row['sites']:
            require(site in sites, 'row site was not enumerated: ' + site)
            require(site not in owned, 'site owned twice: ' + site)
            owned[site] = name
        statuses[row['status']] += 1
    for item in inventory['excluded']:
        site = item['site']
        require(site in sites and site not in owned and item['reason'].strip(), 'invalid exclusion: ' + site)
        owned[site] = 'excluded'
    require(set(owned) == sites, 'unowned enumerated sites: ' + str(sorted(sites - set(owned))))
    guards = inventory.get('guards', {})
    require(all(ADDRESS.match(a) and re.fullmatch(r'([0-9a-f]{2})+', b) for a, b in guards.items()),
            'invalid instruction guard')
    checked = original_bytes(binary, guards) if binary else 0
    return dict(status='verified', task=task, rows=len(ids), enumerated_sites=len(sites),
                excluded=len(inventory['excluded']), statuses=dict(sorted(statuses.items())),
                owners=dict(sorted(owners.items())), guards=len(guards), guards_checked=checked)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory', type=Path)
    parser.add_argument('--binary', type=Path, help='retail game.dll; checks every instruction guard')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = validate(json.loads(args.inventory.read_text()), json.loads(CORPUS.read_text()), TODO.read_text(),
                      args.binary)
    output = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    sys.stdout.write(output)


if __name__ == '__main__':
    main()
