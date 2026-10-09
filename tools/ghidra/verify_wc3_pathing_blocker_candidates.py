#!/usr/bin/env python3
"""Corpus entry for the preserved ROUTE-02.2 research oracle."""
from pathlib import Path
import runpy

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).with_name('research') /
                      'verify_route02_2_blockers.py'), run_name='__main__')
