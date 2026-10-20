#!/usr/bin/env python3
"""Check the BASE-03.2 field/slot/global read-write inventory.

The inventory must contain exactly one row for every field of every recovered
layout. Covered rows need an attributed writer, a reader or address-taker and a
resolvable anchor (closed task, Payoff, oracle or doc heading); every other row
names one remaining open task. Virtual slots, globals and unnamed prefix
accesses follow the same ownership rule. No game or native code is executed.
"""
import argparse
from functools import cache
from collections import Counter
import gzip
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_inventory as inv

TYPES = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json'
INVENTORY = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-fields-inventory-1.27.json.gz'
FIELD_STATUSES = {'covered', 'partial', 'uncovered'}
SLOT_STATUSES = FIELD_STATUSES | {'absent'}
GLOBAL_STATUSES = FIELD_STATUSES | {'not-pathing-state'}
ANCHORS = {'doc-anchor-mentioning-accessor', 'payoff-via-accessor-comment', 'closed-task-via-accessor-comment',
           'oracle-exercising-accessor', 'payoff', 'closed-task'}
ADDRESS = re.compile(r'^6f[0-9a-f]{6}$')
require = inv.require


def payoffs(cache={}):
    if not cache:
        # Payoff numbers are cited by the docs or by the saved mapper comments.
        sources = list(inv.DOCS.glob('*.md')) + [ROOT / 'tools/ghidra/MapPathfinding.java']
        text = '\n'.join(p.read_text() for p in sources)
        cache.update({m: True for m in re.findall(r'Payoff ?(\d+)', text)})
    return cache


@cache
def doc(anchor):
    name, _, heading = anchor.partition('#')
    path = ROOT / name
    require(name.startswith('docs/games/warcraft-3/') and path.is_file(), 'unknown evidence document: ' + anchor)
    require(not heading or heading in inv.anchors(path), 'unknown evidence heading: ' + anchor)


def check_evidence(items, oracles, closed):
    anchored = False
    for item in items:
        kind = item['type']
        if kind == 'layout-evidence':
            require(item['text'].strip(), 'empty layout evidence')
            continue
        require(kind in ANCHORS, 'unknown evidence type: ' + kind)
        anchored = True
        if kind == 'doc-anchor-mentioning-accessor':
            doc(item['anchor'])
        elif kind.startswith('payoff'):
            require(re.fullmatch(r'Payoff(\d+)', item['id']) and item['id'][6:] in payoffs(),
                    'unknown Payoff evidence: ' + item['id'])
        elif kind.startswith('closed-task'):
            require(item['id'] in closed, 'evidence task is not closed: ' + item['id'])
        else:
            require(item['id'] in oracles, 'unknown oracle evidence: ' + item['id'])
    return anchored


def owned(row, statuses, open_tasks, owners, label):
    require(row['status'] in statuses, 'invalid %s status: %s' % (label, row['status']))
    if row['status'] in ('partial', 'uncovered'):
        require(row['owner'] in open_tasks, '%s owner is not a remaining task: %s' % (label, row.get('owner')))
        owners[row['owner']] += 1
    else:
        require(row.get('owner') is None, 'resolved %s row keeps an owner' % label)


def validate(inventory, types, corpus, todo):
    require(inventory.get('task') == 'BASE-03.2' and inventory.get('game_sha256') == inv.SHA, 'unsupported inventory')
    states = {m[2]: m[1] for m in inv.TASK.finditer(todo)}
    open_tasks = {k for k, v in states.items() if v == ' ' and k != 'BASE-03.2'}
    closed = {k for k, v in states.items() if v == 'x'}
    oracles = {e['id'] for e in corpus['entries']}
    expected = Counter((l['name'], f['offset'], f['name']) for l in types['layouts'] for f in l['fields'])
    actual = Counter((r['struct'], int(r['offset'], 16), r['name']) for r in inventory['fields'])
    require(actual == expected, 'layout field set differs: missing %s, extra %s' %
            (sorted(expected - actual)[:5], sorted(actual - expected)[:5]))
    ids, owners, statuses = set(), Counter(), Counter()
    for row in inventory['fields']:
        require(row['id'].startswith('B032-') and row['id'] not in ids, 'invalid or duplicate field row')
        ids.add(row['id'])
        owned(row, FIELD_STATUSES, open_tasks, owners, 'field')
        anchored = check_evidence(row['evidence'], oracles, closed)
        for access in row['writers'] + row['readers'] + row.get('address_taken', []):
            require(ADDRESS.match(access['address']), 'invalid accessor address')
        if row['status'] == 'covered':
            require(row['writers'] and (row['readers'] or row.get('address_taken')) and anchored,
                    'covered field lacks writer/reader/anchor: ' + row['id'])
        statuses['field:' + row['status']] += 1
    for row in inventory['virtual_slots']:
        require(ADDRESS.match(row['vtable']), 'invalid vtable')
        owned(row, SLOT_STATUSES, open_tasks, owners, 'slot')
        for ref in row['evidence']['closed_tasks']:
            require(ref in closed, 'slot evidence task is not closed: ' + ref)
        for ref in row['evidence']['doc_anchors']:
            doc(ref)
        if row['status'] == 'covered':
            require(any(row['evidence'][k] for k in ('closed_tasks', 'payoffs', 'doc_anchors')),
                    'covered slot lacks evidence')
        statuses['slot:' + row['status']] += 1
    for row in inventory['globals']:
        require(ADDRESS.match(row['address']), 'invalid global')
        owned(row, GLOBAL_STATUSES, open_tasks, owners, 'global')
        statuses['global:' + row['status']] += 1
    for row in inventory['layout_gaps_accessed']:
        require(row['status'] == 'uncovered', 'unnamed prefix access cannot be resolved here')
        owned(row, FIELD_STATUSES, open_tasks, owners, 'gap')
        statuses['gap:' + row['status']] += 1
    require(all(ADDRESS.match(a) for a in inventory['function_scope']), 'invalid attribution scope')
    return dict(status='verified', task='BASE-03.2', fields=len(ids), virtual_slots=len(inventory['virtual_slots']),
                globals=len(inventory['globals']), prefix_gaps=len(inventory['layout_gaps_accessed']),
                function_scope=len(inventory['function_scope']), statuses=dict(sorted(statuses.items())),
                owners=dict(sorted(owners.items())))


def load(path=INVENTORY):
    return json.loads(gzip.decompress(path.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=INVENTORY)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = validate(load(args.inventory), json.loads(TYPES.read_text()), json.loads(inv.CORPUS.read_text()),
                      inv.TODO.read_text())
    output = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    sys.stdout.write(output)


if __name__ == '__main__':
    main()
