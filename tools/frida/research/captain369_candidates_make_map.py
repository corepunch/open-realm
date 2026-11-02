#!/usr/bin/env python3
"""Public candidate classification and selection-clock budget probe."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain369_candidate_builder',HERE/'captain369_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.TASK='captain369_candidates'
setup=wrapper.SETUP.replace('1000.0,1000.0','-1936.0,-1424.0')
for i,raw in enumerate(('hfoo','htow','hbar','ugol','Hpal','hrif')):
 setup+='\ncall PauseUnit(CreateUnit(Player(1),\'%s\',%s,%s,0.0),true)'%(raw,-1712.0+128*(i%2),-512.0-256*(i//2))
setup+='\ncall RshMark("label=candidates-created")'
builder.VARIANTS={'player':dict(members=6,end=260,schedule=[(90,('native',setup)),(100,('cmd',8)),(210,('cmd',7))]),'suicide':dict(members=6,end=260,schedule=[(90,('native',setup)),(100,('cmd',11)),(210,('cmd',7))])}
if __name__=='__main__':builder.main()
