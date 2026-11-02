#!/usr/bin/env python3
"""Revalidate original wrap/reuse contracts and actual repeated engine journeys."""
from research.e2e212_contract import ROOT, CATEGORIES, validate
from verify_wc3_pathing_e2e_variants import main

FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-e2e-wrap212-1.27.json'

if __name__ == '__main__':
    main(FIXTURE, validate, CATEGORIES, 'wc3_e2e212')
