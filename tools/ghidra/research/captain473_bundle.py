#!/usr/bin/env python3
"""Build the immutable retreat473 evidence bundle and strict verifier spec.

Every accepted observation and observer-free control (two public families and the retail UI
save/load family) is stored literally (raw JSONL and Preload text). Failed or diagnostic
launches are listed in the spec with their file hashes and reasons but excluded from
acceptance. Retail save files stay in the research archive; their hashes are pinned.
"""
import argparse, gzip, hashlib, json, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_captain473 as v  # noqa: E402

ARCHIVE = Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/retreat473')
FAMILIES = [('retreat473', 'captures', ['explore-a-3', 'observe-a-4', 'observe-a-5'], 'control-a-1'),
            ('retreat473b', 'captures', ['b-observe-1', 'b-observe-2'], 'b-control-1')]
SAVELOAD = ('saveload', ['sl-explore-1', 'sl-explore-2', 'sl-observe-3'], ['sl-control-1', 'sl-control-2'])
FAILED = {'captures/explore-a-1': 'loading never advanced past the map briefing; no public marker; superseded',
          'captures/explore-a-2': 'owned loading-input helper timed out after 5 s in env C; no public marker; superseded'}
PINS = ['games/warcraft-3/game/skills/s_move.c', 'games/warcraft-3/game/tests/t_captain_retreat473.c',
        'games/warcraft-3/game/tests/fixtures/retail_retreat473.h', 'games/warcraft-3/game/tests/fixtures/retail_retreat473b.h',
        'games/warcraft-3/tests/resources-src/Scripts/retreat473.j', 'games/warcraft-3/tests/resources-src/Scripts/retreat473b.j',
        'games/warcraft-3/tests/resources-src/Scripts/retreat473.ai',
        'tools/frida/research/retreat473_capture.py', 'tools/frida/research/retreat473_saveload_capture.py',
        'tools/frida/research/retreat473_observer.js', 'tools/frida/research/retreat473_make_map.py',
        'tools/frida/research/retreat473_probe.j', 'tools/frida/research/retreat473b_probe.j',
        'tools/frida/research/retreat473_probe.ai', 'tools/frida/research/retreat473_expected.py',
        'tools/frida/research/retreat473_saveload_compare.py', 'tools/frida/research/retreat473_summary.py',
        'tools/ghidra/research/captain473_guards.py', 'tools/ghidra/fixtures/research/captain473/original-guards.json',
        'tools/ghidra/fixtures/research/captain473/R473a.w3m', 'tools/ghidra/fixtures/research/captain473/R473b.w3m']


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def capture(directory, stem, role):
    raw = (ARCHIVE / directory / f'{stem}.jsonl').read_text()
    pre = (ARCHIVE / directory / f'{stem}-preload.txt').read_bytes().decode('utf-8')
    return dict(name=stem, role=role, raw_jsonl=raw, preload=pre)


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    families = []
    for name, directory, observed, control in FAMILIES:
        caps = [capture(directory, s, 'observation') for s in observed] + [capture(directory, control, 'control')]
        meta = json.loads(caps[0]['raw_jsonl'].splitlines()[0])
        families.append(dict(name=name, map_sha256=meta['source_sha256']['map'], captures=caps))
    directory, observed, controls = SAVELOAD
    sl = dict(captures=[capture(directory, s, 'observation') for s in observed] + [capture(directory, s, 'control') for s in controls])
    bundle = dict(task='GROUP-03.4.7.3', scene='retreat473', families=families, saveload=sl)
    gz = gzip.compress(json.dumps(bundle, sort_keys=True).encode(), mtime=0)
    out = ROOT / 'tools/ghidra/fixtures/retail-captain473-1.27.json.gz'
    out.write_bytes(gz)
    guards = json.loads((ROOT / 'tools/ghidra/fixtures/research/captain473/original-guards.json').read_text())
    counts = v.validate_runtime(bundle, {k: p.read_text() for k, p in v.HEADERS.items()})
    saves = {f.name: sha(f) for f in sorted((ARCHIVE / 'saveload').glob('*-save.w3z'))}
    failed = [dict(name=k, reason=r, files={f.name: sha(f) for f in sorted((ARCHIVE / k).parent.glob(Path(k).name + '*'))})
              for k, r in FAILED.items()]
    spec = dict(version=2, task='GROUP-03.4.7.3', game_sha256=v.SHA, bundle='tools/ghidra/fixtures/retail-captain473-1.27.json.gz',
                bundle_sha256=hashlib.sha256(gz).hexdigest(), engine_tests=v.TESTS, counts=counts,
                guard_count=len(guards['instructions']), functions=guards['functions'], instructions=guards['instructions'],
                pins={p: sha(ROOT / p) for p in PINS}, retail_save_files=saves, excluded_captures=failed,
                scope='Moving six-Footman CaptainAttack roster; RemoveUnit of four members far from home (and a fifth during '
                      'retreat): 9d7760 reevaluates 9d08e0 per removal, retreat bit2 at two members, GoHome range500 at '
                      'speed500, 9d16c0 retreat branch reissues each non-BTLF member with 9d87d0(d0012,null,authored home) '
                      'and retains an identical null-target d0012 head; the actor stops inside its range, its goal event '
                      'clears bit2 and the members arrive independently. Word-exact public getters and Captain snapshots. '
                      'Native retail UI save/load at five save points (two inside the retreat bit window) resumes '
                      'identically to the uninterrupted journey; engine cold saves at the same points continue word-exactly.',
                exclusions=['combat-triggered retreat and engagement journeys',
                            'smart/attackonce/action3 append and board-head branches of 9d87d0 (implemented from assembly, '
                            'no public witness)', 'native b8/c8 counters', 'transport fallback of 9d87d0 target conversion',
                            'retail save/load of combat, transport and help states'])
    v.SPEC.write_text(json.dumps(spec, indent=1, sort_keys=True) + '\n')
    print(json.dumps(counts))


if __name__ == '__main__':
    main()
