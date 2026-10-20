#!/usr/bin/env python3
"""Separate global candidate enumeration from mutable owned-pool order."""
import importlib.util
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain369_transfer_builder',HERE/'captain369_candidates_make_map.py')
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)
builder=wrapper.builder;builder.TASK='captain369_transfer'
builder.VARIANTS={'roundtrip':dict(members=6,end=260,schedule=[(90,('native',wrapper.setup)),(95,('native','call RshRoundTripCandidates()')),(100,('cmd',8)),(210,('cmd',7))])}
if __name__=='__main__':builder.main()
