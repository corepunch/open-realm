#!/usr/bin/env python3
"""Public KillUnit variant of the mixed target event registry scene."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('target363_death_parent',HERE/'target363_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder
builder.VARIANTS={'death':dict(members=6,end=230,schedule=wrapper.common+[(180,wrapper.native(
    'call RshMark("label=mixed-death-before")\ncall KillUnit(udg_RshEnemy)\ncall RshMark("label=mixed-death-after")'))])}
if __name__=='__main__':builder.main()
