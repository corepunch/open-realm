#!/usr/bin/env python3
"""Public fresh acquisition: mana-only Captain contact without scripted damage."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain358_contact_builder',HERE/'captain358_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.VARIANTS={'acquire':dict(members=6,end=300,schedule=[(110,('cmd',5)),(120,('cmd',6)),(160,('native',
 'call RshMark("label=acquire-before")\ncall SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)\ncall SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)\ncall SetUnitPosition(udg_RshEnemy,GetUnitX(udg_RshUnit[0])+160.0,GetUnitY(udg_RshUnit[0]))\ncall RshMark("label=acquire-after")'))])}
builder.VARIANTS['clean']=dict(members=6,end=300,schedule=[(110,('cmd',5)),(120,('cmd',6)),(150,('native','\n'.join("call UnitRemoveAbility(udg_RshUnit[%d],'Aslo')"%i for i in range(6))+"\ncall UnitRemoveAbility(udg_RshEnemy,'Aslo')")),*builder.VARIANTS['acquire']['schedule'][2:]])
if __name__=='__main__':builder.main()
