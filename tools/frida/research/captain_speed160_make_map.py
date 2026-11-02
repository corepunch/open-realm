#!/usr/bin/env python3
"""Public early CaptainAttack controls, using the existing policy map packer."""
import importlib.util
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('captain_policy_builder', HERE / 'GROUP-03.4.7.3_make_map.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
builder.VARIANTS = {
    'away': dict(members=6, end=180, schedule=[(12, ('cmd', 6))]),
    'home': dict(members=6, end=180, schedule=[(12, ('cmd', 3))]),
    'slow': dict(members=6, end=180, schedule=[(11, ('native', 'call SetUnitMoveSpeed(udg_RshUnit[0],180.0)')), (12, ('cmd', 6))]),
    'drop': dict(members=6, end=180, schedule=[(11, ('native', '\n'.join(
        "call UnitAddAbility(udg_RshUnit[%d],'Adro')" % i for i in range(6)))), (12, ('cmd', 6))]),
    'nomove': dict(members=6, end=180, schedule=[(11, ('native',
        "call SetUnitMoveSpeed(udg_RshUnit[0],180.0)\ncall UnitRemoveAbility(udg_RshUnit[0],'Amov')")), (12, ('cmd', 6))]),
}
original_schedule = builder.schedule_jass
def schedule_jass(schedule):
    return '\n'.join('    elseif udg_RshTick==%d then\n        %s' % (tick, action[1].replace('\n', '\n        '))
                     if action[0] == 'native' else original_schedule([(tick, action)])
                     for tick, action in schedule)
builder.schedule_jass = schedule_jass
legacy_markers = '--legacy-markers' in sys.argv
if legacy_markers:
    sys.argv.remove('--legacy-markers')
original_instrument = builder.mwpm.instrument
def instrument(source, probe, mode):
    if not legacy_markers:
        probe = probe.replace('    call SetCameraPosition',
            '    call RshMark("label=order-ids board="+I2S(OrderId("board"))+" load="+I2S(OrderId("load")))\n    call SetCameraPosition')
    return original_instrument(source, probe, mode)
builder.mwpm.instrument = instrument
if __name__ == '__main__':
    builder.main()
    output = Path(sys.argv[sys.argv.index('--output') + 1]).with_suffix('.json')
    provenance = json.loads(output.read_text())
    provenance['task'] = 'payoff160'
    provenance['wrapper_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    provenance['legacy_markers'] = legacy_markers
    output.write_text(json.dumps(provenance, indent=1) + '\n')
