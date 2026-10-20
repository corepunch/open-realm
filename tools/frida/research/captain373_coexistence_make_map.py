#!/usr/bin/env python3
"""Public Wind Walk, ShowUnit and repeated Captain class-query composition."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain373_coexist_builder',HERE/'captain369_candidates_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder;builder.TASK='captain373_coexistence'
original="call PauseUnit(CreateUnit(Player(1),'Hpal',-1712.0,-1024.0,0.0),true)"
replacement="set udg_RshUnit[7]=CreateUnit(Player(1),'Hpal',-1712.0,-1024.0,0.0)\ncall UnitAddAbility(udg_RshUnit[7],'AOwk')\ncall SetUnitState(udg_RshUnit[7],UNIT_STATE_MANA,100.0)\ncall IssueImmediateOrder(udg_RshUnit[7],\"windwalk\")"
assert original in wrapper.setup
setup=wrapper.setup.replace(original,replacement)
def marker(label):return 'if IsUnitInvisible(udg_RshUnit[7],Player(0)) then\ncall RshMark("label='+label+' invisible=1")\nelse\ncall RshMark("label='+label+' invisible=0")\nendif\ncall RshMark("label='+label+' buff="+I2S(GetUnitAbilityLevel(udg_RshUnit[7],\'BOwk\')))'
builder.VARIANTS={'composition':dict(members=6,end=300,schedule=[(90,('native',setup)),(95,('native','call PauseUnit(udg_RshUnit[7],true)\n'+marker('invisible-only'))),(100,('cmd',11)),(130,('native','call ShowUnit(udg_RshUnit[7],false)\n'+marker('hidden-invisible'))),(140,('cmd',11)),(175,('native','call ShowUnit(udg_RshUnit[7],true)\n'+marker('shown-invisible'))),(185,('cmd',11)),(260,('cmd',7))])}
if __name__=='__main__':builder.main()
