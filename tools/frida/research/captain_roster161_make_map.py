#!/usr/bin/env python3
"""Public Captain 24/25-member admission, replacement and withdrawal scenes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('captain_policy_builder', HERE / 'GROUP-03.4.7.3_make_map.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
builder.VARIANTS = {
    'twentyfour': dict(members=24, end=260, schedule=[(12, ('cmd', 6)), (160, ('remove', 0, 0)), (180, ('cmd', 6))]),
    'twentyfive': dict(members=25, end=260, schedule=[(12, ('cmd', 6)), (160, ('remove', 0, 0)), (180, ('cmd', 6))]),
}

if __name__ == '__main__':
    builder.main()
    output = Path(sys.argv[sys.argv.index('--output') + 1]).with_suffix('.json')
    provenance = json.loads(output.read_text())
    provenance['task'] = 'payoff161'
    provenance['wrapper_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    output.write_text(json.dumps(provenance, indent=1) + '\n')
