#!/usr/bin/env python3
"""Revalidate original dynamic/pursuit streams and repeat actual game journeys."""
from research.e2e206_contract import ROOT, CATEGORIES, validate
from verify_wc3_pathing_e2e_variants import main

FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-e2e-dynamic206-1.27.json'

if __name__ == '__main__':
    main(FIXTURE, validate, CATEGORIES, 'wc3_e2e206')
