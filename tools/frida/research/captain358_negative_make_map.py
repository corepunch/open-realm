#!/usr/bin/env python3
"""Public melee c0 delta inversion after a current-mana mutation."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain358_negative_builder',HERE/'captain358_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder;builder.TASK='captain358_negative'
builder.VARIANTS={'negative':dict(members=6,end=300,schedule=[(110,('cmd',5)),(120,('cmd',6)),(145,('cmd',7)),
 (155,('native','call SetUnitState(udg_RshUnit[0],UNIT_STATE_MANA,0.0)\ncall RshMark("label=zero-mana value="+R2S(GetUnitState(udg_RshUnit[0],UNIT_STATE_MANA)))\ncall RshRemove(0,0)')),(156,('cmd',5)),(160,('native','call RshMark("label=negative-damage-before")\ncall UnitDamageTarget(udg_RshEnemy,udg_RshUnit[1],1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)\ncall RshMark("label=negative-damage-after")'))])}
if __name__=='__main__':builder.main()
