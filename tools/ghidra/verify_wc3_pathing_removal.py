#!/usr/bin/env python3
"""Strict corpus entry for pending-removal separation and engine lifecycle."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'frida/research'))
from removal204_verify import main

if __name__ == '__main__':
    main()
