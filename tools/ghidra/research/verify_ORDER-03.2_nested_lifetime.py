#!/usr/bin/env python3
"""ORDER-03.2 original-code oracle: nested dispatch, destruction of the dispatching agent's
subscriptions / references from a subscriber, unwind and callback lifetime.

Reuses the harness and independent model of verify_ORDER-03.1_subscriber_dispatch.py (imported,
unchanged): unmodified 6f0725b0 / 6f0728c0 / 6f071dc0 / 6f071e00 / 6f071d00 and the original pools.
CONTROLLED (forced-state, labelled): supplied agents, callback objects and handler bodies that only
CALL the original routines; supplied Storm allocation / CRT memset. Agent destruction is modelled
as the two original effects it has on a dispatching table (Agent_ClearEventSubscribers 6f071d00, as
done by AgentPayload_ReleaseFromWrapper 6f04c2c0, and dropping the last agent reference); the live
public RemoveUnit path performs the clear later at depth 0 (see the handoff).
"""
import argparse
import hashlib
import importlib.util
import json
import random
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('o31', Path(__file__).with_name('verify_ORDER-03.1_subscriber_dispatch.py'))
o31 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(o31)
E, F, G = o31.E, o31.F, o31.G


def case(name, n_cbs, scripts, ops=None, setup=None, cb_refs=None, agents=1, agent_refs=1):
    setup = setup if setup is not None else [('reg', 0, E, 0xd0000 + i, i) for i in range(4)]
    return dict(name=name, group='03.2', agents=agents, agent_refs=agent_refs, cb_refs=cb_refs or [1] * n_cbs,
                setup=setup, ops=ops or [('dispatch', 0, E), ('dispatch', 0, E)], scripts=scripts)


def named_cases():
    S = lambda *ops: [(1, list(ops))]
    cs = [
        case('clear-then-nested-same-agent', 6, {0: S(('clear', 0), ('dispatch', 0, E), ('reg', 0, E, 0xd0005, 5), ('dispatch', 0, E))}),
        case('destroy-agent-then-nested', 6, {1: S(('clear', 0), ('decref', 'agent', 0), ('dispatch', 0, E))}, ops=[('dispatch', 0, E)]),
        case('nested-unregisters-outer-current', 6, {1: [(1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 1)])]}),
        case('nested-unregisters-outer-next', 6, {1: [(1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 2)])]}),
        case('nested-remove-and-reregister', 6, {1: [(1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 2), ('reg', 0, E, 0xd00f2, 2)])]}),
        case('nested-unregisters-outer-earlier', 6, {2: [(1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 0)])]}),
        case('depth-four-growth-deferred', 40, {0: [(1, [('dispatch', 0, E)]), (1, [('dispatch', 0, E)]), (1, [('dispatch', 0, E)]),
                                                     (1, [('reg', 0, 0x80300 + 4 * i, 0xd1000 + i, 6 + i) for i in range(20)])]}),
        case('nested-other-bucket-tombstone-survives', 6, {0: S(('reg', 0, G, 0xd0005, 5), ('dispatch', 0, E)), 1: [(1, []), (1, [('unreg', 0, G, 5)])]}),
        case('self-last-ref-then-nested', 6, {1: S(('unreg', 0, E, 1), ('dispatch', 0, E))}, cb_refs=[0] * 6),
        case('cross-agent-clear-during-other-dispatch', 6, {0: S(('dispatch', 1, E)), 4: S(('clear', 0))}, agents=2,
             setup=[('reg', 0, E, 0xd0000 + i, i) for i in range(4)] + [('reg', 1, E, 0xd0004, 4)]),
        case('agent-refs-unwind-order', 6, {0: S(('dispatch', 0, E)), 1: [(1, []), (1, [('decref', 'agent', 0)])]},
             ops=[('dispatch', 0, E)], agent_refs=1),
    ]
    return cs


def random_cases(seed, count):
    rng = random.Random(seed)
    events = [E, F, G, 0x8024d]
    out = []
    for n in range(count):
        n_cbs, agents = 8, 2
        setup = [('reg', rng.randrange(agents), rng.choice(events[:3]), 0xd0000 + rng.randrange(0x100), rng.randrange(n_cbs))
                 for _ in range(rng.randint(3, 8))]
        budget = [8]

        def ops_for():
            ops = []
            for _ in range(rng.randint(0, 3)):
                k = rng.random()
                if k < 0.25:
                    ops.append(('reg', rng.randrange(agents), rng.choice(events), 0xd0000 + rng.randrange(0x100), rng.randrange(n_cbs)))
                elif k < 0.45:
                    ops.append(('unreg', rng.randrange(agents), rng.choice(events[:3]), rng.choice([None] + list(range(n_cbs)))))
                elif k < 0.8 and budget[0] > 0:
                    budget[0] -= 1
                    ops.append(('dispatch', rng.randrange(agents), rng.choice(events[:3])))
                elif k < 0.9:
                    ops.append(('clear', rng.randrange(agents)))
                else:
                    ops.append(('decref', 'cb', rng.randrange(n_cbs)))
            return ops
        scripts = {c: [(rng.randint(0, 1), ops_for()) for _ in range(rng.randint(0, 4))] for c in range(n_cbs)}
        ops = [('dispatch', rng.randrange(agents), rng.choice(events[:3])) for _ in range(rng.randint(1, 3))]
        out.append(dict(name='random-%d-%d' % (seed, n), group='random', agents=agents, agent_refs=2,
                        cb_refs=[rng.randint(0, 2) for _ in range(n_cbs)], setup=setup, ops=ops, scripts=scripts))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--random', type=int, default=400)
    ap.add_argument('--seed', type=int, default=3032)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != o31.SHA:
        ap.error('requires game.dll 1.27.1.7085')
    cases = named_cases() + random_cases(args.seed, args.random)
    results, mismatches = [], []
    stats = {'deliveries': 0, 'nested_dispatches': 0, 'max_depth': 0, 'destroy_cb': 0, 'destroy_agent': 0, 'fault': 0}
    for c in cases:
        md = o31.jsonable(o31.Model(c).run())
        # Reference counts are u32 words in the original; the model keeps Python ints.
        md['cb_refs'] = [x & 0xffffffff for x in md['cb_refs']]
        md['agent_refs'] = [x & 0xffffffff for x in md['agent_refs']]
        md['log'] = [e[:5] + [e[5] & 0xffffffff, e[6] & 0xffffffff] if e[0] == 'deliver' else e for e in md['log']]
        try:
            o = o31.jsonable(o31.Original(binary, c).run())
        except Exception as error:
            o = {'fault': str(error)}
            stats['fault'] += 1
        ok = 'fault' not in o and all(o[k] == md[k] for k in ('log', 'tables', 'agent_refs', 'cb_refs')) \
            and o['pool_live_delta'] == md['pool_live_delta'] and o['bucket_pool_live'] == md['bucket_live']
        if not ok:
            mismatches.append({'case': c['name'], 'original': o, 'model': md})
        log = md['log']
        stats['deliveries'] += sum(1 for e in log if e[0] == 'deliver')
        stats['nested_dispatches'] += max(0, sum(1 for e in log if e[0] == 'dispatch') - len(c['ops']))
        stats['max_depth'] = max([stats['max_depth']] + [e[3] or 0 for e in log if e[0] == 'deliver'])
        stats['destroy_cb'] += sum(1 for e in log if e[0] == 'destroy-cb')
        stats['destroy_agent'] += sum(1 for e in log if e[0] == 'destroy-agent')
        if c['group'] != 'random':
            results.append({'name': c['name'], 'group': c['group'], 'case': o31.jsonable(c), 'original': o})
    report = {'binary_sha256': o31.SHA, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'harness_sha256': hashlib.sha256(Path(spec.origin).read_bytes()).hexdigest(),
              'cases': len(cases), 'named': len(results), 'random': args.random, 'seed': args.seed,
              'mismatches': len(mismatches), 'stats': stats, 'named_results': results, 'mismatch_detail': mismatches[:5]}
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('cases', 'named', 'random', 'mismatches', 'stats')}))
    if args.expected:
        exp = json.loads(args.expected.read_text())
        frozen = {r['name']: r['original'] for r in exp['oracle']['nested_results']}
        if frozen != {r['name']: r['original'] for r in results}:
            print('named results differ from frozen expected', file=sys.stderr)
            sys.exit(2)
    sys.exit(1 if mismatches else 0)


if __name__ == '__main__':
    main()
