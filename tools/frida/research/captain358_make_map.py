#!/usr/bin/env python3
"""Public Captain combat notification with mana-only melee rosters."""
import importlib.util
from pathlib import Path
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('captain358_builder', HERE / 'GROUP-03.4.7.3_make_map.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
builder.TASK = 'captain358'
builder.VARIANTS = {
 'mana': dict(members=6,end=350,schedule=[(110,('cmd',5)),(120,('cmd',6)),(160,('native',
   'call RshMark("label=damage-before")\ncall UnitDamageTarget(udg_RshEnemy,udg_RshUnit[0],1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)\ncall RshMark("label=damage-after")'))]),
}
original_schedule=builder.schedule_jass
def schedule_jass(schedule):
 return '\n'.join('    elseif udg_RshTick==%d then\n        %s' % (tick,action[1].replace('\n','\n        ')) if action[0]=='native' else original_schedule([(tick,action)]) for tick,action in schedule)
builder.schedule_jass=schedule_jass
if __name__=='__main__': builder.main()
