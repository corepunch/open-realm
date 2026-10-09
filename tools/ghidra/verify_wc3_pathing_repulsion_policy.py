#!/usr/bin/env python3
"""Reproduce the unchanged policy oracle and require its full frozen report hash."""
import argparse
import hashlib
import importlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent


def verify(binary, report):
    sys.path.insert(0, str(HERE / 'research'))
    expected = json.loads((HERE / 'fixtures/research/SEP-01.3-expected.json').read_text())['oracle']
    original = report.with_name(report.stem + '-original.json')
    argv = sys.argv
    try:
        sys.argv = ['verify_SEP-01.2_policy.py', '--binary', str(binary), '--report', str(original)]
        spec = importlib.util.spec_from_file_location('retail_policy_oracle',
                    HERE / 'research/verify_SEP-01.2_policy.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.main()
    finally:
        sys.argv = argv
    digest = hashlib.sha256(original.read_bytes()).hexdigest()
    if digest != expected['sha256']:
        raise ValueError('full original policy report differs from frozen retail evidence')
    data = json.loads(original.read_text())
    # The original inert controls use unaligned synthetic RNG byte offsets.
    # Re-run those geometries with recorded producer state for production C.
    valid_path = HERE / 'fixtures/retail-repulsion-inert-producers-1.27.json'
    valid = json.loads(valid_path.read_text())
    oracle = importlib.import_module('sep_research_oracle').Oracle(binary)
    for row in valid['pairs']:
        owner, vector, drew = oracle.pair(oracle.settings[5], row['ownerBefore'],
                                         row['source'], row['candidate'], row['prior'])
        if (owner, vector, drew) != (row['ownerAfter'], row['vector'], row['randomBranch']):
            raise ValueError('valid-state inert pair differs from frozen original output')
    result = dict(passed=True, binary_sha256=data['binary_sha256'], original_sha256=digest,
                  packed_word_cases=data['packed_word_cases'], inert_pairs=len(data['inert_pairs']),
                  inert_tails=len(data['inert_tails']), pairs=data['harness_selfcheck']['pairs'],
                  tails=data['harness_selfcheck']['tails'], producer_pairs=len(valid['pairs']),
                  producer_sha256=hashlib.sha256(valid_path.read_bytes()).hexdigest())
    eligibility = importlib.import_module('verify_separation_eligibility').verify(binary)
    if eligibility != json.loads((HERE / 'fixtures/retail-separation-eligibility-1.27.json').read_text()):
        raise ValueError('complete original separation eligibility differs from frozen evidence')
    result['eligibility_cases'] = len(eligibility['cases'])
    live = importlib.import_module('verify_immobile_separation')
    result.update(live.verify(HERE / 'fixtures/retail-repulsion-immobile-1.27.json.gz', oracle))
    report.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.binary.resolve(), args.report.resolve()), indent=2))
