#!/usr/bin/env python3
"""Isolate a timed move/terrain edit in a copied Human02Interlude terrain archive."""
import argparse
import hashlib
import json
import math
import re
import subprocess
import tempfile
from pathlib import Path

SCENARIOS = {'open': 0, 'wall': 1, 'insert': 2, 'remove': 3, 'remove_reorder': 4,
             'gate': 5, 'gate_off': 6, 'gate_retarget': 7, 'gate_disable': 8, 'owner_change': 9, 'follow': 10, 'follow_shift': 11, 'follow_walk': 12, 'follow_invisible': 13, 'follow_fog': 14, 'follow_fog_reacquire': 15, 'blocked_goal': 16, 'crowd': 17, 'crowd_air': 18, 'widget_lifecycle': 19, 'turn': 20}


def instrument(script, probe, scenario, remove_tick=50, gate_y=-800.0, gate_exit_y=-240.0):
    if not 11 <= remove_tick < 300:
        raise ValueError('removal tick must follow the order and precede completion')
    if not all(math.isfinite(v) and -3072 <= v <= 5120 for v in (gate_y, gate_exit_y)):
        raise ValueError('gate Y coordinates must be finite and inside the source map')
    # Exact expected call sites prevent accidental edits to another campaign map.
    for old, new in [('call CreateAllUnits(  )', 'call PathProbeInit()'),
                     ('call InitCustomTriggers(  )', '// Path probe: scripted campaign orders disabled.'),
                     ('call RunInitializationTriggers(  )', '// Path probe: timer owns this experiment.')]:
        if script.count(old) != 1:
            raise ValueError('requires one Human02Interlude main call: ' + old)
        script = script.replace(old, new)
    if 'gg_rct_SorcAFight = Rect( -1856.0, -352.0, -1728.0, -224.0 )' not in script:
        raise ValueError('unexpected source map geometry')
    probe = probe.replace('@SCENARIO@', str(SCENARIOS[scenario])).replace('@NAME@', scenario)
    probe = probe.replace('@REMOVE_TICK@', str(remove_tick))
    probe = probe.replace('@GATE_Y@', str(float(gate_y))).replace('@GATE_EXIT_Y@', str(float(gate_exit_y)))
    block = re.search(r'^globals\n(.*?)^endglobals\n', probe, re.M | re.S)
    if block is None or script.count('\nendglobals') != 1:
        raise ValueError('expected one globals block per script')
    script = script.replace('\nendglobals', '\n' + block.group(1) + 'endglobals', 1)
    return script.replace('\nendglobals', '\nendglobals\n' + probe[block.end():], 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool'))
    parser.add_argument('--scenario', choices=SCENARIOS, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--remove-tick', type=int, default=50)
    parser.add_argument('--gate-y', type=float, default=-800.0)
    parser.add_argument('--gate-exit-y', type=float, default=-240.0)
    args = parser.parse_args()
    if args.base.resolve() == args.output.resolve() or args.output.exists():
        parser.error('output must be a new file, distinct from the source map')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        parser.error('requires the original 512-byte wrapped campaign map')
    probe = Path(__file__).with_name('wc3_pathfinding_probe.j').read_text()
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    if members.count('war3map.j') != 1:
        parser.error('source must have exactly one war3map.j')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wc3-path-map-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if member == 'war3map.j':
                source = data.decode('utf-8').replace('\r\n', '\n')
                data = instrument(source, probe, args.scenario, args.remove_tick, args.gate_y, args.gate_exit_y).encode()
                args.output.with_suffix('.j').write_bytes(data)
            path = root / str(i)
            path.write_bytes(data)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = {'base_sha256': hashlib.sha256(original).hexdigest(), 'scenario': args.scenario,
              'remove_tick': args.remove_tick,
              'gate_y': args.gate_y, 'gate_exit_y': args.gate_exit_y,
              'map_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(), 'members': members,
              'changed_members': ['war3map.j'], 'container': 'rebuilt MPQ with original HM3W header; signature not retained'}
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
