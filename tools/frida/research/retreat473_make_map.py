#!/usr/bin/env python3
"""Build the GROUP-03.4.7.3 moving-roster retreat journey probe from the flat arena.

Stock Footmen; no custom unit or ability rows. The map timer creates/removes units and
samples public getters; the AI script performs every Captain native.
"""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True, help='Shared315-foot-2-final.w3m flat arena')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--probe', type=Path, default=HERE / 'retreat473_probe.j')
    p.add_argument('--ai', type=Path, default=HERE / 'retreat473_probe.ai')
    a = p.parse_args()
    if a.output.exists():
        p.error('fresh output required')
    tool = str(ROOT / 'build/bin/mpqtool')

    def cat(n):
        return subprocess.check_output([tool, '-mpq', str(a.base), 'cat', n])
    members = subprocess.check_output([tool, '-mpq', str(a.base), 'ls']).decode().splitlines()
    empty = struct.pack('<III', 2, 0, 0)
    source = cat('war3map.j').decode().replace('\r\n', '\n')
    probe = a.probe.read_text()
    glob, func = probe.split('endglobals', 1)
    source = source.replace('endglobals', glob.removeprefix('globals') + '\nendglobals', 1)
    begin = source.index('function S315Mark')
    end = source.index('function InitGlobals', begin)
    script = source[:begin] + func + '\n' + source[end:]
    overrides = {'war3map.j': script.encode(), 'war3map.w3u': empty, 'war3map.w3a': empty,
                 'war3map.wpm': b'MP3W' + struct.pack('<III', 0, 64, 64) + bytes(4096),
                 r'Scripts\retreat473.ai': a.ai.read_bytes()}
    with tempfile.TemporaryDirectory(prefix='retreat473-') as tmp:
        tmp = Path(tmp)
        payload = tmp / 'payload.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, n in enumerate(dict.fromkeys(members + list(overrides))):
            if n == '(listfile)' or n.endswith(('/', chr(92))):
                continue
            f = tmp / str(i)
            f.write_bytes(overrides[n] if n in overrides else cat(n))
            cmd.extend([str(f), n])
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        a.output.write_bytes(a.base.read_bytes()[:512] + payload.read_bytes())
    meta = dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),
                map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),
                sources={q.name: hashlib.sha256(q.read_bytes()).hexdigest() for q in [Path(__file__), a.probe, a.ai]})
    a.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta))


if __name__ == '__main__':
    main()
