#!/usr/bin/env python3
"""Build RS-MAP-04.2 live maps by wrapping make_wc3_pathfinding_map.py unchanged.

The existing builder is imported and run with its own target_overlap scenario; only
the probe text it reads for that scenario is substituted by the research probe
(tools/frida/research/map04_2_target_edit_probe.j). Output must be a new file.
"""
import hashlib, json, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import make_wc3_pathfinding_map as builder  # noqa: E402

PROBE = HERE / 'map04_2_target_edit_probe.j'
_read_text = pathlib.Path.read_text


def patched(self, *a, **k):
    if self.name == 'wc3_target_overlap_probe.j':
        return _read_text(PROBE, *a, **k)
    return _read_text(self, *a, **k)


if __name__ == '__main__':
    out = pathlib.Path(sys.argv[sys.argv.index('--output') + 1])
    if '--scenario' not in sys.argv or sys.argv[sys.argv.index('--scenario') + 1] != 'target_overlap':
        sys.exit('only --scenario target_overlap is wrapped')
    pathlib.Path.read_text = patched
    try:
        builder.main()
    finally:
        pathlib.Path.read_text = _read_text
    side = out.with_suffix('.json')
    data = json.loads(side.read_text())
    data['research_probe'] = PROBE.name
    data['research_probe_sha256'] = hashlib.sha256(PROBE.read_bytes()).hexdigest()
    data['research_wrapper_sha256'] = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
    data['builder_sha256'] = hashlib.sha256((HERE.parent / 'make_wc3_pathfinding_map.py').read_bytes()).hexdigest()
    side.write_text(json.dumps(data, indent=2) + '\n')
