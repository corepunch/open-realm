"""Bind cross-feature categories to unchanged retail contracts and game journeys."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CATEGORIES = {
    'formation': ('wc3_e2e205.selected_independent_and_passage_formations',
                  ['live-selected-formation-passage', 'live-formation-selection-independent']),
    'crowd': ('wc3_e2e205.mixed_and_disabled_crowds_with_saved_continuation', ['oracle-crowds']),
    'gate': ('wc3_e2e205.group_gate_skip_disconnection_and_chained_activation',
             ['live-gate-group-open-captures-261004', 'live-gate-group-wall-captures-261004',
              'live-gate-chain-captures-261004']),
}
SOURCES = [
    'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json',
    'tools/ghidra/fixtures/retail-formation-passage169-1.27.json',
    'tools/ghidra/fixtures/retail-formation-selection170-1.27.json',
    'games/warcraft-3/game/tests/fixtures/retail_formation_passage_169.h',
    'games/warcraft-3/game/tests/fixtures/retail_formation_selection_170.h',
    'tools/ghidra/fixtures/retail-separation-crowds-1.27.json.gz',
    'tools/ghidra/fixtures/retail-separation-crowd-inputs-1.27.json.gz',
    'games/warcraft-3/game/tests/retail_separation_crowds.h',
    'tools/ghidra/fixtures/retail-gate-group-open-live-1.27.json',
    'tools/ghidra/fixtures/retail-gate-group-wall-live-1.27.json',
    'tools/ghidra/fixtures/retail-gate-chain-live-1.27.json',
    'games/warcraft-3/game/tests/retail_gate_group.h',
    'games/warcraft-3/game/tests/retail_gate_chain.h',
]


def fingerprint(entry):
    return hashlib.sha256(json.dumps(entry, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate(spec, manifest, root=ROOT):
    return validate_bindings(spec, manifest, CATEGORIES, SOURCES, 'E2E-01.4', root)


def validate_bindings(spec, manifest, categories, sources, task, root=ROOT):
    if (spec['version'] != 1 or spec['task'] != task or spec['parent'] != sources[0] or
        spec['engine_repeats'] != 2 or spec['engine_editions'] != ['classic', 'tft']):
        raise ValueError('unsupported combined baseline contract')
    base = json.loads((root / sources[0]).read_text())
    if spec['build'] != base['build'] or spec['build']['game_sha256'] != manifest['target']['game_sha256']:
        raise ValueError('cross-feature binary provenance differs')
    if set(spec['pins']) != set(sources):
        raise ValueError('cross-feature literal source inventory differs')
    for path, pin in spec['pins'].items():
        if hashlib.sha256((root / path).read_bytes()).hexdigest() != pin:
            raise ValueError('changed existing retail expectations: ' + path)
    if len(spec['categories']) != len(categories):
        raise ValueError('missing cross-feature category')
    entries = {row['id']: row for row in manifest['entries']}
    selected = []
    for row, (name, (test, ids)) in zip(spec['categories'], categories.items(), strict=True):
        if row['id'] != name or row['test'] != test or row['entries'] != ids or not row['scope'] or not row['completion']:
            raise ValueError('cross-feature category mapping differs')
        if set(row['entry_sha256']) != set(ids):
            raise ValueError('missing original sub-contract')
        for identity in ids:
            entry = entries[identity]
            if fingerprint(entry) != row['entry_sha256'][identity] or 'L' not in entry['evidence']:
                raise ValueError('original sub-contract differs: ' + identity)
            selected.append(entry)
    expected = sum(len(ids) for _, ids in categories.values())
    if len(selected) != expected or len({row['id'] for row in selected}) != expected:
        raise ValueError('duplicated or omitted retail branch')
    return selected
