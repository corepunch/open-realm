#!/usr/bin/env python3
"""TARGET-03.2 reacquisition map: registers target03r_probe.j (5 scenes, Preload prefix "T3R ") with the unchanged
target021_make_map.py builder and runs it. Research tool (new file); the shared builder is not modified."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import target021_make_map as builder  # noqa: E402

builder.PROBES['target03r'] = ('target03r_probe.j', 'rs-target03r.txt', 5)

if __name__ == '__main__':
    builder.main()
