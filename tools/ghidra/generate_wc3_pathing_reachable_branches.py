#!/usr/bin/env python3
"""Publish complete per-branch ownership from ExportReachableBranchRecords.java.

First write --functions, run the read-only Ghidra exporter with that input and a
fresh JSONL output, then pass the JSONL as --export to produce --output. Function
order follows the existing BASE-01 closure; import leaves have no instructions.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import audit_wc3_pathing_reachable as audit


def build(inventory, raw):
    audit.require(hashlib.sha256(raw).hexdigest() == audit.BRANCH_EXPORT_SHA,
                  'fresh export differs from reviewed complete branch export')
    functions = {r['address']:r for r in inventory['functions']}
    branches, summaries = [], {}
    for line in raw.splitlines():
        row = json.loads(line)
        if 'function_summary' in row:
            audit.require(row['function_summary'] not in summaries, 'duplicate function summary')
            summaries[row['function_summary']] = row['branches']
        else:
            owner = functions[row['function']]['owner']
            audit.require(owner is not None, 'branch needs an explicit remaining behavior owner')
            branches.append(dict(row, owner=owner, evidence=[]))
    audit.require(set(summaries) == {a for a in functions if not a.startswith('EXTERNAL:')} and
                  all(functions[a]['branches'] == n for a,n in summaries.items()),
                  'complete branch export differs from original function totals')
    return dict(inventory, version=2, branches=branches, branch_export=dict(
        path=audit.BRANCH_EXPORT, sha256=audit.BRANCH_EXPORT_SHA,
        functions=len(summaries), branches=len(branches)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=audit.INVENTORY)
    parser.add_argument('--functions', type=Path, help='write function-address input for the Ghidra exporter')
    parser.add_argument('--export', dest='export_path', type=Path, help='read the complete fresh JSONL export')
    parser.add_argument('--output', type=Path, help='write enriched gzip inventory')
    args = parser.parse_args()
    inventory = audit.load(args.inventory)
    if args.functions:
        args.functions.write_text(''.join(r['address']+'\n' for r in inventory['functions']
                                          if not r['address'].startswith('EXTERNAL:')))
    if args.export_path:
        if not args.output:
            parser.error('--export requires --output')
        raw = args.export_path.read_bytes()
        if args.export_path.suffix == '.gz':
            raw = gzip.decompress(raw)
        result = build(inventory, raw)
        args.output.write_bytes(gzip.compress((json.dumps(result, sort_keys=True, separators=(',',':'))+'\n').encode(), mtime=0))
        print(f"Published {len(result['branches'])} individual branches; behavior remains owned by open tasks.")
    elif args.output:
        parser.error('--output requires --export')
    elif not args.functions:
        parser.error('choose --functions or --export')


if __name__ == '__main__':
    main()
