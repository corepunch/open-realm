#!/usr/bin/env python3
"""Selected-primary removal must rearm the saved Captain query policy."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain369_loss_builder',HERE/'captain369_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder;builder.TASK='captain369_loss'
setup=wrapper.SETUP.replace('1000.0,1000.0','-1936.0,-1424.0')
builder.VARIANTS={'removed':dict(members=6,end=240,schedule=[(90,('native',setup)),(100,('cmd',8)),(120,('native','call RemoveUnit(udg_RshEnemy)\ncall RshMark("label=primary-removed")')),(190,('cmd',7))])}
if __name__=='__main__':builder.main()
