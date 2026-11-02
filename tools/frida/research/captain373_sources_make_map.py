#!/usr/bin/env python3
"""Public hidden structure survey for the strategic owner query."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain373_source_builder',HERE/'captain369_candidates_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder;builder.TASK='captain373_sources'
original="call PauseUnit(CreateUnit(Player(1),'htow',-1584.0,-512.0,0.0),true)"
replacement="set udg_RshUnit[7]=CreateUnit(Player(1),'htow',-1584.0,-512.0,0.0)\ncall PauseUnit(udg_RshUnit[7],true)\ncall ShowUnit(udg_RshUnit[7],false)\ncall RshMark(\"label=source-hidden\")"
assert original in wrapper.setup
setup=wrapper.setup.replace(original,replacement)
builder.VARIANTS={'hidden':dict(members=6,end=260,schedule=[(90,('native',setup)),(100,('cmd',11)),(210,('cmd',7))])}
if __name__=='__main__':builder.main()
