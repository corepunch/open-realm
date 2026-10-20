#!/usr/bin/env python3
"""Public near-home and flee-off contact with a hostile paused source."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain358_engagement_builder',HERE/'captain358_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
CONTACT='call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)\ncall SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)\ncall SetUnitPosition(udg_RshEnemy,-1936.0,-1424.0)\ncall RshMark("label=engagement-before")\ncall UnitDamageTarget(udg_RshEnemy,udg_RshUnit[0],1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)\ncall RshMark("label=engagement-after")'
builder.VARIANTS={
 'near':dict(members=6,end=300,schedule=[(100,('cmd',3)),(120,('native',CONTACT))]),
 'far':dict(members=6,end=300,schedule=[(120,('cmd',6)),(160,('native',CONTACT))]),
 'lost':dict(members=6,end=300,schedule=[(100,('cmd',3)),(120,('native',CONTACT)),(180,('native','call RshMark("label=source-lost-before")\ncall RemoveUnit(udg_RshEnemy)\ncall RshMark("label=source-lost-after")'))]),
}
if __name__=='__main__':builder.main()
