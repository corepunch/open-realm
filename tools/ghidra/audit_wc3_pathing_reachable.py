#!/usr/bin/env python3
"""Check the BASE-03.1 reachable function/branch inventory.

Rows are the direct-call plus resolved-virtual closure of the BASE-01 entry
points. Every function has one status: covered rows need an oracle that guards
or executes an address inside the body; partial rows a mapper row, plate or
narrow fixture citation; excluded rows an out-of-scope category. Every row names
one remaining open task (covered rows for their unguarded branches), as does every computed call site
whose receiver class could not be resolved. Branch totals must equal the rows.
Version2 verifies every individual conditional jump against a complete pinned
Ghidra export and its open behavior owner. Version1 verifies function rows and
aggregate counts only and cannot certify BASE-03.1 closure.
"""
import argparse
from collections import Counter
import gzip
import hashlib
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


def validate(inventory, corpus, todo, forced, binary=None):
    require(inventory.get('task') == 'BASE-03.1' and inventory.get('binary_sha256') == inv.SHA, 'unsupported inventory')
    states = {m[2]: m[1] for m in inv.TASK.finditer(todo)}
    has_branches = 'branches' in inventory and 'branch_export' in inventory
    require(has_branches or states.get('BASE-03.1') == ' ',
            'BASE-03.1 closure needs individual branch records; this schema only verifies function rows and totals')
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
    branch_result = validate_branches(inventory, rows, open_tasks, binary) if has_branches else {}
    return dict(status='verified', task='BASE-03.1', closure_verified=has_branches,
                runtime_branch_coverage_certified=False, **branch_result, roots=len(roots), functions=len(rows),
                branches=totals['branches'], guarded_branches=totals['guarded_branches'],
                unresolved_sites=len(inventory['unresolved_sites']),
                statuses=dict(sorted(Counter(r['status'] for r in rows.values()).items())),
                owners=dict(sorted(owners.items())))



# Complete, read-only Ghidra export; source instructions are checked separately
# against the original DLL by --binary. Guarding bytes never implies execution.
BRANCH_EXPORT = 'tools/ghidra/fixtures/retail-pathfinding-reachable-branches-1.27.jsonl.gz'
BRANCH_EXPORT_SHA = 'ec85c45562e30defdc3cb2a1fbc2c3fc2045c6d4f50d790e8c2bda7a14fe0032'
BRANCH_FIELDS = {'function', 'site', 'bytes', 'mnemonic', 'taken', 'fallthrough'}


def conditional_edges(site, raw):
    instruction = bytes.fromhex(raw)
    require((len(instruction) == 2 and 0x70 <= instruction[0] <= 0x7f) or
            (len(instruction) == 6 and instruction[0] == 0x0f and 0x80 <= instruction[1] <= 0x8f),
            'unsupported conditional jump: ' + site)
    condition = instruction[0] & 15 if len(instruction) == 2 else instruction[1] & 15
    displacement = int.from_bytes(instruction[1:] if len(instruction) == 2 else instruction[2:],
                                  'little', signed=True)
    fallthrough = int(site, 16) + len(instruction)
    names = ['JO','JNO','JC','JNC','JZ','JNZ','JBE','JA','JS','JNS','JP','JNP','JL','JGE','JLE','JG']
    return names[condition], [f'{(fallthrough + displacement) & 0xffffffff:08x}'], f'{fallthrough:08x}'


def load_branch_export(metadata):
    require(metadata == dict(path=BRANCH_EXPORT, sha256=BRANCH_EXPORT_SHA, functions=7380, branches=21306),
            'branch export identity differs')
    raw = gzip.decompress((ROOT / BRANCH_EXPORT).read_bytes())
    require(hashlib.sha256(raw).hexdigest() == BRANCH_EXPORT_SHA, 'original branch export differs')
    branches, summaries = {}, {}
    for line in raw.splitlines():
        row = json.loads(line)
        if 'function_summary' in row:
            address = row['function_summary']
            require(address not in summaries, 'duplicate exported function summary')
            summaries[address] = row['branches']
        else:
            require(set(row) == BRANCH_FIELDS and row['site'] not in branches, 'duplicate/invalid exported branch')
            decoded = conditional_edges(row['site'], row['bytes'])
            require(decoded == (row['mnemonic'], row['taken'], row['fallthrough']), 'exported branch edges differ')
            branches[row['site']] = row
    counts = Counter(r['function'] for r in branches.values())
    require(len(branches) == 21306 and len(summaries) == 7380 and
            all(counts[a] == n for a,n in summaries.items()) and set(counts) <= set(summaries),
            'incomplete branch export')
    return branches, summaries


def validate_branches(inventory, functions, open_tasks, binary=None):
    require(inventory.get('version') == 2, 'unsupported individual branch schema')
    original, summaries = load_branch_export(inventory['branch_export'])
    require(set(summaries) == {a for a in functions if not a.startswith('EXTERNAL:')} and
            all(functions[a]['branches'] == n for a,n in summaries.items()) and
            all(r['branches'] == 0 for a,r in functions.items() if a.startswith('EXTERNAL:')),
            'function branch counts differ from complete export')
    sites, owners, guards = set(), Counter(), {}
    for row in inventory['branches']:
        site = row['site']
        require(site not in sites, 'duplicate individual branch: ' + site)
        sites.add(site)
        require(site in original and set(row) == BRANCH_FIELDS | {'owner','evidence'} and
                all(row[k] == original[site][k] for k in BRANCH_FIELDS),
                'branch instruction/function/edges differ from export: ' + site)
        # This publication assigns every branch to pending behavior work. A
        # function-level oracle or unreachability label cannot certify its edges.
        require(row['owner'] in open_tasks and row['owner'] == functions[row['function']]['owner'],
                'incorrect individual branch task link: ' + site)
        require(row['evidence'] == [], 'function evidence cannot certify individual branch execution: ' + site)
        owners[row['owner']] += 1
        guards[site] = row['bytes']
    require(sites == set(original), 'missing individual branch records')
    checked = inv.original_bytes(binary, guards) if binary else 0
    return dict(individual_branches=len(sites), branch_guards_checked=checked,
                branch_owners=dict(sorted(owners.items())), branch_export_sha256=BRANCH_EXPORT_SHA)


def load(path=INVENTORY):
    return json.loads(gzip.decompress(path.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=INVENTORY)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--binary', type=Path, help='check all individual branch bytes against the retail DLL')
    args = parser.parse_args()
    result = validate(load(args.inventory), json.loads(inv.CORPUS.read_text()), inv.TODO.read_text(),
                      json.loads(FORCED.read_text()), args.binary)
    output = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    sys.stdout.write(output)


if __name__ == '__main__':
    main()
