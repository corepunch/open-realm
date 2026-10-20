#!/usr/bin/env python3
"""Join the MAP-04.2 exit inventory to fresh native reports and executed tests.

This is a coverage audit. It does not expand the contracts of the input oracles.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-map342-coverage-1.27.json'
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-exclusion-inputs-1.27.json.gz'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(spec, static, verified, executed):
    if spec.get('version') != 1 or spec.get('task') != 'MAP-04.2':
        raise ValueError('unsupported coverage contract')
    if spec['binary_sha256'] != static['binary_sha256']:
        raise ValueError('binary differs')
    expected = {row['name']: row for row in static['scopes']}
    actual = {row['name']: row for row in spec['scopes']}
    if len(actual) != len(spec['scopes']) or set(actual) != set(expected):
        raise ValueError('scope inventory differs')
    for name, row in actual.items():
        native = expected[name]
        for key in ('entry', 'acquires', 'releases'):
            if row[key] != native[key]:
                raise ValueError('scope boundary differs: ' + name)
        if row['reported_exits'] != native['exits_while_held'] or native['missing_from_function_cfg']:
            raise ValueError('exit inventory differs: ' + name)
        if not row['oracles'] or not set(row['oracles']) <= verified:
            raise ValueError('missing native evidence: ' + name)
        if not row['contract'].strip():
            raise ValueError('missing restoration obligation')
        if not row['tests'] and name != 'unreferenced_mixed_query':
            raise ValueError('missing engine restoration assertion: ' + name)
        for edition in ('classic', 'tft'):
            if not set(row['tests']) <= executed[edition]:
                raise ValueError('unexecuted restoration test: ' + name + '/' + edition)
    return dict(passed=True, task=spec['task'], static_scopes=len(actual),
                reachable_scopes=sum(bool(row['tests']) for row in actual.values()),
                required_tests=len({test for row in actual.values() for test in row['tests']}),
                native_oracles=len(verified),
                executed_tests={edition: len(names) for edition, names in executed.items()})


EXTRA_TESTS = [
    'pathfinding.fine_consumers_hold_captured_self_and_restore_every_result',
    'pathfinding.fresh_waypoint219_holds_self_after_fine_search_and_restores_outer_depth',
    'pathfinding.portal188_placement_holds_self_through_admission_and_exhaustion',
    'pathfinding.blocker202_holds_captured_self_through_ordered_resolution',
    'pathfinding.blocker202_public_move_keeps_outer_scope_through_resolution',
    'pathfinding.coarse_scopes_match_complete_native_overlapping_hierarchy_matrix',
    'pathfinding.coarse_scope_observation_stages_match_original_alias_and_null_cases',
    'pathfinding.fine_scopes_restore_nested_counters_aliases_and_complete_native_windows',
]


def run_extra(directory):
    import verify_wc3_pathing_work242 as runner
    directory.mkdir()  # Refuse pre-existing reports.
    previous = runner.TESTS
    try:
        runner.TESTS = EXTRA_TESTS
        counts = runner.run_engine(ROOT / 'build/bin/openwarcraft3-tests', ROOT / 'build/tests',
                                   directory / 'scope-engine.json')
    finally:
        runner.TESTS = previous
    metadata = dict(counts=counts, tests=EXTRA_TESTS,
                    test_binary_sha256=digest(ROOT / 'build/bin/openwarcraft3-tests'),
                    game_library_sha256=digest(ROOT / 'build/lib/libgame-wc3-test.so'),
                    xml={path.name: digest(path) for path in directory.glob('*.xml')})
    (directory / 'scope-engine.json').write_text(json.dumps(metadata, indent=2) + '\n')


def engine_reports(directory, edition):
    # Older focused verifiers emit one report per edition, while newer ones
    # shard each edition. Both contain actual executed restoration assertions.
    return sorted(set(directory.glob('*-' + edition + '.xml')) |
                  set(directory.glob('*-' + edition + '-*.xml')))


def audit(results, extra):
    spec = json.loads(SPEC.read_text())
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    name = next(name for name in bundle['files'] if name.endswith('scope-exits-MAP-04.2.json'))
    raw = bundle['files'][name].encode()
    if hashlib.sha256(raw).hexdigest() != bundle['sha256'][name]:
        raise ValueError('static inventory payload changed')
    static = json.loads(raw)
    corpus = json.loads((results / 'corpus-results.json').read_text())
    required = {name for row in spec['scopes'] for name in row['oracles']}
    if not corpus['passed'] or corpus['completed'] != corpus['requested']:
        raise ValueError('unfinished evidence run')
    verified = set()
    reports = {}
    for row in corpus['results']:
        if row['id'] not in required:
            continue
        path = Path(row['report'])
        if path.parent != results.resolve() or digest(path) != row['report_sha256']:
            raise ValueError('native report changed')
        if not row['verified_expected_status'] or row['exit'] != row['expected_exit']:
            raise ValueError('native check failed')
        report = json.loads(path.read_text())
        if report.get('binary_sha256') != spec['binary_sha256']:
            raise ValueError('native binary differs')
        for field, artifact in [('test_binary_sha256', 'build/bin/openwarcraft3-tests'),
                                ('game_library_sha256', 'build/lib/libgame-wc3-test.so')]:
            if field in report and report[field] != digest(ROOT / artifact):
                raise ValueError('engine changed since evidence run')
        verified.add(row['id'])
        reports[row['id']] = row['report_sha256']
    # The runner records the verifier sources before executing them. Check all
    # recorded Python sources, including transitive original-code helpers.
    for name, sha in corpus['source_sha256'].items():
        if name.endswith('.py') and digest(ROOT / name) != sha:
            raise ValueError('verifier changed since evidence run: ' + name)
    extra_meta = json.loads((extra / 'scope-engine.json').read_text())
    if (extra_meta['tests'] != EXTRA_TESTS or
        extra_meta['test_binary_sha256'] != digest(ROOT / 'build/bin/openwarcraft3-tests') or
        extra_meta['game_library_sha256'] != digest(ROOT / 'build/lib/libgame-wc3-test.so') or
        extra_meta['xml'] != {path.name: digest(path) for path in extra.glob('*.xml')}):
        raise ValueError('additional engine evidence changed')
    executed = {edition: set() for edition in ('classic', 'tft')}
    xml_hashes = {}
    for directory in (results, extra):
        for edition in executed:
            for path in engine_reports(directory, edition):
                suite = ET.parse(path).getroot()
                if any(suite.attrib[key] != '0' for key in ('failures', 'errors', 'skipped')):
                    raise ValueError('failed engine test: ' + str(path))
                executed[edition].update(test.attrib['name'] for test in suite.findall('.//testcase'))
                xml_hashes[str(path)] = digest(path)
    output = validate(spec, static, verified, executed)
    output.update(binary_sha256=spec['binary_sha256'], coverage_sha256=digest(SPEC),
                  corpus_results_sha256=digest(results / 'corpus-results.json'),
                  native_reports=reports, engine_reports=xml_hashes, limits=spec['limits'])
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--extra-engine-results', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--run-extra-engine', action='store_true')
    args = parser.parse_args()
    if args.report.exists():
        parser.error('fresh output required')
    if args.run_extra_engine:
        run_extra(args.extra_engine_results)
    result = audit(args.results.resolve(), args.extra_engine_results.resolve())
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items()
                      if key not in ('native_reports', 'engine_reports')}))


if __name__ == '__main__':
    main()
