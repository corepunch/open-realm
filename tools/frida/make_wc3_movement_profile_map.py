#!/usr/bin/env python3
"""Use the existing authored-type clones with a dedicated public Move probe."""
import importlib.util
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location('profile_builder',HERE/'research/base021_make_map.py')
BUILDER=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(BUILDER)

if __name__=='__main__':
    sys.argv+=['--probe',str(HERE/'wc3_movement_profile_probe.j')]
    BUILDER.main()
