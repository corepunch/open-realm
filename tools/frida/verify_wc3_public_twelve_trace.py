#!/usr/bin/env python3
"""Verify the complete public twelve-member decision/commit lifetime."""
from verify_wc3_public_pair_trace import canonical, verify_group, main


def verify_twelve(rows,fixture):
    return verify_group(rows,fixture,dict(commits=[297,138,190,102,135,126,88,243,158,111,160,205],
        passes=297,marker='twelve-marker',prefix='PATHDOZEN',
        scope='Public twelve-member point group: complete repeated original phases and arithmetic. Whole-engine parity is a separate normal-frame regression; supplied scene geometry remains explicit.'))


if __name__=='__main__': main(verify_twelve)
