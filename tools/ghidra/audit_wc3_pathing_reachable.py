#!/usr/bin/env python3
"""Check the BASE-03.1 reachable function/branch inventory.

Rows are the direct-call plus resolved-virtual closure of the BASE-01 entry
points. Every function has one status: covered rows need an oracle that guards
or executes an address inside the body; partial rows a mapper row, plate or
narrow fixture citation; excluded rows an out-of-scope category. Every row names
one remaining open task (covered rows for their unguarded branches), as does every computed call site
whose receiver class could not be resolved. Branch totals must equal the rows.
"""
import argparse
from collections import Counter
import gzip
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_inventory as inv

INVENTORY = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-reachable-inventory-1.27.json.gz'
FORCED = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-forced-position-inventory-1.27.json'
STATUSES = {'covered', 'partial', 'uncovered', 'excluded', 'unreachable'}
ADDRESS = re.compile(r'^6f[0-9a-f]{6}$')
FUNCTION = re.compile(r'^(6f[0-9a-f]{6}|EXTERNAL:[0-9a-f]{8})$')  # imports are closure leaves
require = inv.require


def validate(inventory, corpus, todo, forced):
    require(inventory.get('task') == 'BASE-03.1' and inventory.get('binary_sha256') == inv.SHA, 'unsupported inventory')
    states = {m[2]: m[1] for m in inv.TASK.finditer(todo)}
    open_tasks = {k for k, v in states.items() if v == ' ' and k != 'BASE-03.1'}
    oracles = {e['id'] for e in corpus['entries']}
    rows, owners, totals = {}, Counter(), Counter()
    for row in inventory['functions']:
        address = row['address']
        require(FUNCTION.match(address) and address not in rows, 'invalid or duplicate function: ' + address)
        rows[address] = row
        require(row['status'] in STATUSES, 'invalid status: ' + address)
        guarded = [e for e in row['evidence'] if e['kind'] == 'oracle' and e.get('tier') == 'executed-or-guarded']
        for item in row['evidence']:
            if item['kind'] == 'oracle':
                require(item['id'] in oracles, 'unknown oracle evidence: ' + item['id'])
        # Covered functions keep an owner for their unguarded branches; owners are routing hints.
        if row['status'] == 'covered':
            require(guarded, 'covered function needs a guarding oracle: ' + address)
        if row['status'] == 'unreachable':
            require(row['rationale'].strip(), 'unproved unreachable function: ' + address)
        if row['owner'] is not None or row['status'] not in ('covered', 'unreachable'):
            require(row['owner'] in open_tasks, 'owner is not a remaining task: %s %s' % (address, row['owner']))
            owners[row['owner']] += 1
        if row['status'] == 'excluded':
            require(row['category'] not in ('game', '', None), 'excluded function lacks a category: ' + address)
        require(0 <= row['guarded_branches'] <= row['branches'], 'invalid branch coverage: ' + address)
        totals['functions'] += 1
        totals['branches'] += row['branches']
        totals['guarded_branches'] += row['guarded_branches']
    expected = inventory['branch_totals']
    require(all(expected[k] == totals[k] for k in totals), 'branch totals differ from rows')
    roots = {r['address'] for r in inventory['roots']}
    require(roots <= set(rows), 'entry point missing from closure')
    forced_entries = {r['native_entry']['address'] for r in forced['rows'] if r['native_entry']}
    # A registered native without a Ghidra function cannot be expanded; it stays an owned root.
    undefined = {r['address'] for r in inventory.get('undefined_roots', [])}
    for root in inventory.get('undefined_roots', []):
        require(root['reason'].strip() and root['owner'] in open_tasks, 'undefined root lacks reason/owner')
    require(forced_entries <= roots | undefined, 'BASE-01.4 entry point is not a root')
    for site in inventory['unresolved_sites']:
        require(site['function'] in rows and ADDRESS.match(site['site']), 'invalid unresolved site')
        require(site['owner'] in open_tasks, 'unresolved site owner is not a remaining task: ' + site['site'])
        owners['site:' + site['owner']] += 1
    return dict(status='verified', task='BASE-03.1', roots=len(roots), functions=len(rows),
                branches=totals['branches'], guarded_branches=totals['guarded_branches'],
                unresolved_sites=len(inventory['unresolved_sites']),
                statuses=dict(sorted(Counter(r['status'] for r in rows.values()).items())),
                owners=dict(sorted(owners.items())))


def load(path=INVENTORY):
    return json.loads(gzip.decompress(path.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=INVENTORY)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = validate(load(args.inventory), json.loads(inv.CORPUS.read_text()), inv.TODO.read_text(),
                      json.loads(FORCED.read_text()))
    output = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    sys.stdout.write(output)


if __name__ == '__main__':
    main()
