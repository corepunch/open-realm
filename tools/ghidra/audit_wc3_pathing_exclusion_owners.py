#!/usr/bin/env python3
"""Check BASE-03.3 ownership against the current corpus and open task list."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json'
OWNERS = ROOT / 'tools/ghidra/fixtures/retail-pathfinding-exclusion-owners-1.27.json'
TODO = ROOT / 'docs/games/warcraft-3/retail-pathfinding-todo.md'
DISPOSITIONS = {'behavior', 'reachability', 'evidence-boundary', 'superseded-review'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(corpus, inventory, todo):
    require(inventory.get('version') == 1 and inventory.get('task') == 'BASE-03.3',
            'unsupported ownership inventory')
    open_tasks = set(re.findall(r'^- \[ \] \*\*([A-Z][A-Z0-9]*-\d+(?:\.\d+)+)\*\*', todo, re.M))
    expected = {}
    oracle_ids = []
    for entry in corpus['entries']:
        if entry['kind'] != 'oracle':
            continue
        name = entry['id']
        require(name not in oracle_ids, 'duplicate corpus oracle: ' + name)
        oracle_ids.append(name)
        exclusions = entry.get('exclusions', [])
        if isinstance(exclusions, str):
            exclusions = [exclusions]
        require(isinstance(exclusions, list) and all(isinstance(x, str) and x for x in exclusions),
                'invalid corpus exclusions: ' + name)
        for index, text in enumerate(exclusions):
            expected[name, index] = text
    require(set(inventory['oracle_ids']) == set(oracle_ids) and
            len(inventory['oracle_ids']) == len(oracle_ids), 'oracle set changed')
    actual, seen_oracles = {}, set()
    task_counts, dispositions = Counter(), Counter()
    for entry in inventory['entries']:
        name = entry['oracle']
        require(name in oracle_ids and name not in seen_oracles, 'unknown or duplicate inventory oracle: ' + name)
        seen_oracles.add(name)
        require(entry['exclusions'], 'empty inventory entry: ' + name)
        for row in entry['exclusions']:
            index = row['index']
            require(type(index) is int and index >= 0, 'invalid exclusion index')
            key = name, index
            require(key not in actual, 'duplicate exclusion: ' + str(key))
            require(key in expected and row['text'] == expected[key], 'exclusion changed: ' + str(key))
            task = row['task']
            require(task in open_tasks and task != 'BASE-03.3', 'owner is not a remaining task: ' + task)
            disposition = row['disposition']
            require(disposition in DISPOSITIONS, 'invalid disposition: ' + disposition)
            require(isinstance(row['rationale'], str) and row['rationale'].strip(), 'missing ownership rationale')
            actual[key] = row['text']
            task_counts[task] += 1
            dispositions[disposition] += 1
    require(actual == expected, 'unassigned exclusions: ' + str(sorted(set(expected) - set(actual))))
    return dict(status='verified', task='BASE-03.3', oracles=len(oracle_ids),
                oracles_with_exclusions=len(seen_oracles), exclusions=len(actual),
                unassigned=0, owners=dict(sorted(task_counts.items())),
                dispositions=dict(sorted(dispositions.items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = validate(json.loads(CORPUS.read_text()), json.loads(OWNERS.read_text()), TODO.read_text())
    output = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(output)
    print(output, end='')


if __name__ == '__main__':
    main()
