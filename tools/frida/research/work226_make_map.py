#!/usr/bin/env python3
"""Build the SEP-01.2 work separation arena, including a shadow map sized to its terrain."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def unit_rows(data):
    # The inherited formation table has no original edits; append two Peasants.
    import importlib.util
    spec = importlib.util.spec_from_file_location('sep', HERE / 'sep_research_map.py')
    sep = importlib.util.module_from_spec(spec); spec.loader.exec_module(sep)
    sep.UNITS = {'hW26': dict(urpo=2, urpp=17, urpg=17, urpr=17, ucol=16),
                 'hN26': dict(urpo=0, ucol=16)}
    if struct.unpack_from('<I', data)[0] not in (1, 2):
        raise ValueError('unsupported unit modification version')
    result = data[:4] + sep.w3u_rows(struct.pack('<I', 1) + data[4:])[4:]
    # Only the newly appended rows change their template.
    for code in (b'hW26', b'hN26'):
        result = result.replace(b'hfoo' + code, b'hpea' + code)
    return result

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='work226-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(HERE / 'work226_probe.j'), '--preload-output', 'rs-w226.txt',
            '--task', 'SEP-01.2', '--wpm-blocked', '[]', '--replace', 'START=rs-w226-start.txt',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.w3u':
                data = unit_rows(data)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            if member == 'war3map.shd':
                # The shared passage builder reduces terrain to 16x16 tiles.
                # Its inherited campaign shadow member must also become 64x64.
                data = bytes(64 * 64)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
            shadow_size=4096,
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
