#!/usr/bin/env python3
"""GROUP-03.4.7.3 research map builder (new research file; shared builders untouched).

Copies runtime/Human02Interlude-original.w3m, instruments war3map.j with GROUP-03.4.7.3_probe.j via
the existing captain_home `instrument` (imported read-only; Player(0) becomes a computer, campaign
triggers disabled) and packs GROUP-03.4.7.3_probe.ai as Scripts\\wc3_captain_probe.ai.  Only
war3map.j changes; Footmen are stock `hfoo`.  Variants select roster size and the public
CommandAI phase schedule.  Run from the repository root.
"""
import argparse
import hashlib
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TASK = 'GROUP-03.4.7.3'
spec = importlib.util.spec_from_file_location('mwpm', HERE.parent / 'make_wc3_pathfinding_map.py')
mwpm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mwpm)

# tick: ('cmd', phase) sends CommandAI(Player(0),phase,0); ('remove', first, last) removes members.
VARIANTS = {
    # Exploratory schedules 'home'/'large'/'empty' (maps -a/-b) are preserved in the capture provenance.
    # retreat: occupied home without and with SetGroupsFlee (no-op while healthy), removal to two members
    # (autonomous retreat GoHome), CaptainAttack north (arrival GoHome), removal of the final members
    # during that travel, then SetCaptainHome on the empty captain.
    'retreat': dict(members=6, end=620, schedule=[(120, ('cmd', 1)), (170, ('cmd', 2)), (220, ('remove', 0, 3)),
                                                  (400, ('cmd', 3)), (470, ('remove', 4, 5)), (520, ('cmd', 4))]),
    # threshold7: seven hurt members with SetGroupsFlee do not retreat on an occupied home change; removing
    # one (six members, zero healthy) does.
    'threshold7': dict(members=7, end=420, schedule=[(120, ('hurt', 0, 6)), (140, ('cmd', 2)), (200, ('remove', 0, 0))]),
    # interrupt: SetGroupsFlee first; CaptainAttack south is interrupted by an autonomous retreat when removal
    # leaves two members; a second CaptainAttack is interrupted by removing the last members (empty roster).
    'interrupt': dict(members=6, end=420, schedule=[(110, ('cmd', 5)), (150, ('cmd', 6)), (165, ('remove', 0, 3)),
                                                    (300, ('cmd', 6)), (315, ('remove', 4, 5))]),
    # noflee: without SetGroupsFlee only the empty roster goes home (and places its actor).
    'noflee': dict(members=6, end=320, schedule=[(120, ('cmd', 1)), (170, ('remove', 0, 4)), (220, ('remove', 5, 5))]),
}


def schedule_jass(schedule):
    lines = []
    for tick, action in schedule:
        lines.append(f'    elseif udg_RshTick=={tick} then')
        if action[0] == 'cmd':
            lines.append(f'        call RshCommand({action[1]})')
        elif action[0] == 'hurt':
            lines.append(f'        call RshHurt({action[1]},{action[2]})')
        else:
            lines.append(f'        call RshRemove({action[1]},{action[2]})')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--variant', choices=sorted(VARIANTS), required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    v = VARIANTS[args.variant]
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original 512-byte wrapped campaign map')
    probe = (HERE / (TASK + '_probe.j')).read_text()
    for key, value in {'@SCHEDULE@': schedule_jass(v['schedule']), '@END_TICK@': str(v['end']),
                       '@MEMBERS@': str(v['members']), '@VARIANT@': args.variant}.items():
        probe = probe.replace(key, value)
    ai = (HERE / (TASK + '_probe.ai')).read_text().replace('@MEMBERS@', str(max(v['members'], 1)))
    if '@' in probe or '@' in ai:
        raise ValueError('unreplaced placeholder')
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    changed = {}
    with tempfile.TemporaryDirectory(prefix='grp0473-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            new = data
            if member == 'war3map.j':
                new = mwpm.instrument(data.decode('utf-8').replace('\r\n', '\n'), probe, 'captain_home').encode('utf-8')
                changed[member] = hashlib.sha256(new).hexdigest()
                args.output.with_suffix('.j').write_bytes(new)
            path = root / str(i)
            path.write_bytes(new)
            command.extend([str(path), member])
        aip = root / 'ai'
        aip.write_text(ai)
        command.extend([str(aip), 'Scripts\\wc3_captain_probe.ai'])
        args.output.with_suffix('.ai').write_text(ai)
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = dict(task=TASK, variant=args.variant, members_count=v['members'], schedule=v['schedule'], end_tick=v['end'],
                  base_sha256=hashlib.sha256(original).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), members=members,
                  changed_members=changed, ai_sha256=hashlib.sha256(ai.encode()).hexdigest(),
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  probe_template_sha256=hashlib.sha256((HERE / (TASK + '_probe.j')).read_bytes()).hexdigest(),
                  shared_builder_sha256=hashlib.sha256((HERE.parent / 'make_wc3_pathfinding_map.py').read_bytes()).hexdigest(),
                  preload_output='rs-group0473.txt',
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('variant', 'map_sha256')}, indent=1))


if __name__ == '__main__':
    main()
