#!/usr/bin/env python3
"""Verify ORDER-01.12 Harvest/Return current ownership against frozen retail captures and the engine."""
import argparse, hashlib, importlib.util, json
from pathlib import Path
from wc3_pathfinding_engine_tests import run_engine
ROOT = Path(__file__).resolve().parents[2]
SPEC = Path(__file__).with_name('fixtures') / 'retail-harvest012-1.27.json'
PATTERNS = ['wc3_harvest012.*', 'wc3_save.rejects_version_217_before_hidden_miner_order_retention']
ENGINE_EXPECTED = {'tests': 6, 'assertions': 251}


def replay():
    path = Path(__file__).with_name('research') / 'harvest012_replay.py'
    loader = importlib.util.spec_from_file_location('harvest012_replay', path)
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


def validate(spec, binary):
    if spec.get('patterns') != PATTERNS or spec.get('engine_expected') != ENGINE_EXPECTED:
        raise ValueError('Harvest acceptance must retain its public journeys, death owner and save rejection')
    assert spec['task'] == 'ORDER-01.12'
    result = replay().verify(ROOT / spec['captures'], spec, binary)
    assert (result['observations'], result['controls'], result['public_markers']) == (4, 2, 26574), result
    assert result['maps']['H012'] == {'markers': 5027, 'scenes': 21, 'transitions': 232, 'admissions': 129}
    assert result['maps']['I012'] == {'markers': 3831, 'scenes': 11, 'transitions': 118, 'admissions': 70}
    assert result.get('guard_bytes') == 1547
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=Path('build/bin/openwarcraft3-tests'))
    parser.add_argument('--data', type=Path, default=Path('build/tests'))
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    spec = json.loads(SPEC.read_text())
    retail = validate(spec, args.binary)
    engine = run_engine(args.test_binary, args.data, args.report, spec['patterns'])
    assert all(value == spec['engine_expected'] for value in engine.values()), engine
    library = args.test_binary.resolve().parent.parent / 'lib/libgame-wc3-test.so'
    result = {'passed': True, 'status': 'verified', 'binary_sha256': spec['binary_sha256'],
              'game_library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(), 'retail': retail,
              'engine': engine, 'scope': spec['scope']}
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
