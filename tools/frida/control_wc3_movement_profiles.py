#!/usr/bin/env python3
"""Observer-free movement-profile control; exactly150 ordered probe samples."""
import re
import control_wc3_pathfinding as control


def validate(rows):
    samples=[int(re.search(r'tick=(\d+)',row)[1]) for row in rows
             if row.startswith('PATHTRACE ') and ' label=sample ' in row]
    if samples!=list(range(1,151)):
        raise ValueError('requires exactly150 ordered JASS samples')
    if not rows or not rows[-1].startswith('PATHTRACE tick=150 label=complete '):
        raise ValueError('missing movement-profile completion marker')


if __name__=='__main__':
    control.validate=validate
    control.main()
