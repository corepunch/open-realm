#!/usr/bin/env python3
"""ROUTE-05.1: assert blocker-identity lifetime around public RemoveUnit and handle reuse (route03_observer.js
captures taken with --registry-events).

  --kind peer20   tri-converge + RemoveUnit(u0) at probe tick 200 (owner 1090), CreateUnit replacement at tick 201
  --kind req4far  cross-same  + RemoveUnit(A) at tick 330 (owner 1133), replacement far away at tick 331
  --kind req4near cross-same  + RemoveUnit(A) at tick 330, stationary replacement at A's former point at tick 331
Prints a JSON summary; exit 1 on the first failed assertion.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from route03_expected import load  # noqa: E402

NONE = [0xffffffff, 0xffffffff]


class Fail(Exception):
    pass


def check(c, m):
    if not c:
        raise Fail(m)


def index(rows):
    paths = {}
    for r in rows:
        if r.get('event') == 'step-leave':
            paths.setdefault(r['mover']['id'][0], {}).setdefault(r['path']['path'], []).append(r)
    return paths


def waits(rows, slot):
    return [r for r in rows if r.get('event') == 'yield-set' and r['identity'] and r['identity'][0] == slot]


def advances(rows, path):
    return [(r['counter'], r['before']['delay'], r['delay'], r['result']) for r in rows if r.get('event') == 'advance' and r['path'] == path]


def countdown_and_release(rows, path, start, n):
    adv = advances(rows, path)
    cd = [a for a in adv if start < a[0] <= start + n]
    check([a[1] for a in cd] == list(range(n, 0, -1)) and all(a[3] == 1 for a in cd), 'countdown %d..1 for %s after %d: %s' % (n, path, start, cd))
    nxt = [a for a in adv if a[0] == start + n + 1]
    check(len(nxt) == 1, 'single visit after countdown')
    return nxt[0]


def registry_after(rows, label):
    seen = False
    for r in rows:
        if r.get('event') == 'marker' and ('label=' + label) in r['value']:
            seen = True
            continue
        if seen and r.get('event') == 'registry':
            return r
    raise Fail('no registry snapshot after ' + label)


def state(reg, ident):
    for x in reg['ids']:
        if x['id'] == ident:
            return x
    raise Fail('identity %s not watched' % ident)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kind', required=True, choices=('peer20', 'req4far', 'req4near', 'uialt', 'uiplain'))
    ap.add_argument('capture')
    args = ap.parse_args()
    rows = load(args.capture)
    out = dict(capture=args.capture, kind=args.kind)
    try:
        if args.kind in ('uialt', 'uiplain'):
            alt = args.kind == 'uialt'
            clicks = [r for r in rows if r.get('event') == 'ui-click']
            check(len(clicks) == 1 and clicks[0]['alt'] == alt and clicks[0]['returncode'] == 0, 'one owned UI click')
            issued = [r['value'] for r in rows if r.get('event') == 'marker' and 'label=issued' in r['value']]
            check(len(issued) == 2 and all('order=851986' in v for v in issued), 'both units issued move (851986)')
            flags = set()
            for r in rows:
                if r.get('event') == 'yield-decision':
                    for g in r.get('groups', {}).values():
                        flags.add((tuple(g['id']), g['flags'], g['members']))
            check(len({f[0] for f in flags}) == 1 and any(f[2] == 2 for f in flags), 'one shared two-member group')
            check(all(bool(f[1] & 8) == alt for f in flags), 'group bit8 %s' % ('set' if alt else 'clear'))
            ys = [r for r in rows if r.get('event') == 'yield-set']
            check(len(ys) == 1, 'exactly one wait assignment')
            y = ys[0]
            if alt:
                check((y['requested'], y['caller'], y['identity']) == (20, 0x168477, [715, 715]), 'u1 <- u0 peer 20')
            else:
                check((y['requested'], y['caller'], y['identity']) == (4, 0x168498, [733, 733]), 'u0 waits 4 on u1')
            out['group_flags'] = sorted(hex(f[1]) for f in flags)
            out['wait'] = dict(counter=y['counter'], requested=y['requested'], caller=hex(y['caller']), blocker=y['identity'])
            last = {}
            for r in rows:
                if r.get('event') == 'step-leave':
                    last[r['mover']['id'][0]] = r['counter']
            out['last_step_visit'] = last
        elif args.kind == 'peer20':
            blocker = [715, 715]
            w = waits(rows, 715)
            check([(x['counter'], x['requested'], x['caller']) for x in w[:2]] == [(1082, 20, 0x168477), (1084, 20, 0x168477)], 'two peer-20 waits on u0')
            p_u2, p_u1 = w[0]['path'], w[1]['path']
            rem = [r for r in rows if r.get('event') == 'marker' and 'label=after-remove' in r['value']]
            check(len(rem) == 1 and rem[0]['counter'] == 1090, 'RemoveUnit(u0) after owner visit 1090')
            check(state(registry_after(rows, 'after-remove'), blocker)['resolves'], 'u0 identity still resolves right after RemoveUnit')
            s = state(registry_after(rows, 'replace'), blocker)
            check(not s['resolves'] and s['link'] == 0xfffffffe and s['objectGen'] != 715, 'slot 715 reused with a new generation by the replacement creation')
            out['slot715_after_replace'] = dict(objectGen=s['objectGen'])
            r2 = countdown_and_release(rows, p_u2, 1082, 20)
            r1 = countdown_and_release(rows, p_u1, 1084, 20)
            check(r2[0] == 1103 and r2[1] == 0 and r2[3] == 0 and r1[0] == 1105 and r1[3] == 0, 'releases 1103 (u2) and 1105 (u1)')
            stale = [r for r in rows if r.get('event') == 'step-leave' and r['path']['path'] in (p_u1, p_u2) and r.get('blkState')
                     and 1090 < r['counter'] < 1103]
            check(stale and all(not r['blkState']['resolves'] and r['path']['blk'] == blocker for r in stale), 'stale identity kept, unresolvable')
            rel = [r for r in rows if r.get('event') == 'registry-release' and r['slot'] == 715]
            if rel:
                check(rel[0]['counter'] == 1090 and rel[0]['caller'] == 0x1c54a8, 'slot 715 released by PathRegistry_UnregisterAndRelease at 1090')
                out['release'] = dict(counter=rel[0]['counter'], stack=rel[0].get('stack'))
            new = [r for r in rows if r.get('event') == 'step-enter' and r['mover']['id'][0] not in (715, 733, 751)]
            if new:
                out['replacement_mover'] = dict(id=new[0]['mover']['id'], memory_reused=new[0]['mover']['mover'] == [
                    r for r in rows if r.get('event') == 'step-enter' and r['mover']['id'] == blocker][0]['mover']['mover'])
        else:
            blocker = [715, 715]
            w = waits(rows, 715)
            check(len(w) >= 1 and (w[0]['counter'], w[0]['requested'], w[0]['caller']) == (1133, 4, 0x168498), 'B requester-4 on A at 1133')
            pb = w[0]['path']
            rem = [r for r in rows if r.get('event') == 'marker' and 'label=after-remove' in r['value']]
            check(len(rem) == 1 and rem[0]['counter'] == 1133, 'RemoveUnit(A) after owner visit 1133')
            check(state(registry_after(rows, 'after-remove'), blocker)['resolves'], 'A identity still resolves right after RemoveUnit')
            s = state(registry_after(rows, 'replace'), blocker)
            check(not s['resolves'] and s['objectGen'] != 715, 'slot 715 reused with a new generation')
            out['slot715_after_replace'] = dict(objectGen=s['objectGen'])
            nxt = countdown_and_release(rows, pb, 1133, 4)
            col = [r for r in rows if r.get('event') == 'collect' and r['path'] == pb and r['counter'] == 1138]
            check(len(col) == 1, 'collector at 1138')
            if args.kind == 'req4far':
                check(col[0]['tokens'] == [] and nxt[3] == 0 and len(waits(rows, 715)) == 1, 'empty vector at 1138: release without retry')
            else:
                t = col[0]['tokens']
                check(len(t) == 1 and t[0]['vel'] == [0, 0] and t[0]['group'] == NONE and t[0]['id'][0] != 715, 'stationary replacement token at 1138')
                inits = [r for r in rows if r.get('event') == 'retry-init' and r['path'] == pb]
                check(inits and inits[0]['count'] == 7, 'retry init 7 against the replacement')
                out['replacement_token'] = t[0]['id']
            out['release_visit'] = nxt
        out['final'] = [r['value'] for r in rows if r.get('event') == 'marker' and 'label=complete' in r['value']]
    except Fail as e:
        print(json.dumps(dict(out, ok=False, failure=str(e))))
        sys.exit(1)
    print(json.dumps(dict(out, ok=True)))


if __name__ == '__main__':
    main()
