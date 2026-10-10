#!/usr/bin/env python3
"""Build the SEP-01.2 work separation arena, including a shadow map sized to its terrain."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def unit_rows(data):
    if struct.unpack_from('<I', data)[0] not in (1, 2):
        raise ValueError('unsupported unit modification version')
    cursor, ids = 4, set()
    for _ in range(2):
        count_at = cursor
        count = struct.unpack_from('<I', data, cursor)[0]; cursor += 4
        for _ in range(count):
            old, new, fields = struct.unpack_from('<4s4sI', data, cursor); cursor += 12
            ids.add(new)
            for _ in range(fields):
                field, kind = struct.unpack_from('<4sI', data, cursor); cursor += 8
                cursor = data.index(b'\0', cursor) + 1 if kind == 3 else cursor + 4
                cursor += 4
    if cursor != len(data):
        raise ValueError('trailing unit data')
    definitions = []
    for old, new, enabled, builds in [('opeo','oW27',2,'otrb,oB27'),('opeo','oN27',0,'otrb,oB27'),
                                     ('nmpe','nW27',2,'nnfm,nB27'),('nmpe','nN27',0,'nnfm,nB27')]:
        definitions.append((old,new,{'urpo':enabled,'urpp':17,'urpg':17,'urpr':17,'ucol':16.0,'ubui':builds}))
    definitions += [('otrb','oB27',{'ubld':2}),('nnfm','nB27',{'ubld':2})]
    result = data[:count_at] + struct.pack('<I', count + len(definitions)) + data[count_at + 4:]
    for old, new, fields in definitions:
        code = new.encode()
        if code in ids:
            raise ValueError('custom id exists')
        result += old.encode() + code + struct.pack('<I',len(fields))
        for field, value in fields.items():
            kind = 3 if isinstance(value,str) else 2 if isinstance(value,float) else 0
            payload = value.encode()+b'\0' if kind==3 else struct.pack('<f' if kind==2 else '<i',value)
            result += field.encode() + struct.pack('<I',kind) + payload + code
    return result

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='work227-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(HERE / 'work227_probe.j'), '--preload-output', 'rs-w227.txt',
            '--task', 'SEP-01.2', '--wpm-blocked', '[]', '--replace', 'START=rs-w227-start.txt',
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
