#!/usr/bin/env python3
"""Build the stock or authored-DropItemRange point-drop probe map; no engine writes."""
import argparse, hashlib, json, subprocess, tempfile, struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
RANGES = {'stock': 150, 'authored': 150}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--setting', choices=sorted(RANGES), required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mpqtool', type=Path, default=ROOT / 'build/bin/mpqtool')
    a = p.parse_args()
    if a.output.exists():
        p.error('output must be new')
    tool = str(a.mpqtool)

    def cat(name):
        return subprocess.check_output([tool, '-mpq', str(a.base), 'cat', name])
    names = [n for n in cat('(listfile)').decode().splitlines() if not n.endswith('/')]
    stem = 'target-itemgive-v7-' + a.setting
    script = (HERE / 'itemdrop_probe.j').read_text().replace('DROP_RANGE', '%d.0' % RANGES[a.setting])
    script = script.replace('PRELOAD_FILE', stem + '.txt').replace('START_FILE', stem + '-start.txt')
    script += ('\nfunction config takes nothing returns nothing\n call SetPlayers(1)\n call SetTeams(1)\n'
               ' call SetPlayerController(Player(0),MAP_CONTROL_USER)\n'
               ' call SetPlayerSlotAvailable(Player(0),MAP_CONTROL_USER)\nendfunction\n'
               'function main takes nothing returns nothing\n call InitBlizzard()\n call PathProbeInit()\nendfunction\n')
    # Empty object-ability table: the arena keeps stock Inventory/Move rows.
    can_get = 1 if a.setting=='stock' else 0
    inv_override = struct.pack('<II',2,1) + b'AInv' + struct.pack('<II',0,1) + b'inv4' + struct.pack('<iiiiiI',0,1,4,can_get,0,0)
    overrides = {'war3map.j': script.encode(), 'war3map.w3a': inv_override}
    overrides['war3mapMisc.txt'] = ('[Misc]\nDropItemRange=100\nGiveItemRange=%d\nPawnItemRange=%d\n' % (RANGES[a.setting],130)).encode()
    with tempfile.TemporaryDirectory(prefix='target-itemdrop-') as tmp:
        tmp = Path(tmp)
        payload = tmp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, name in enumerate(dict.fromkeys(names + list(overrides))):
            if name == '(listfile)':
                continue
            f = tmp / str(i)
            f.write_bytes(overrides[name] if name in overrides else cat(name))
            cmd.extend([str(f), name])
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        a.output.write_bytes(a.base.read_bytes()[:512] + payload.read_bytes())
    meta = {'can_get_items': can_get, 'setting': a.setting, 'give_range': RANGES[a.setting], 'base_sha256': hashlib.sha256(a.base.read_bytes()).hexdigest(),
            'map_sha256': hashlib.sha256(a.output.read_bytes()).hexdigest(),
            'sources': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in [Path(__file__), HERE / 'itemdrop_probe.j']},
            'overrides': {n: hashlib.sha256(d).hexdigest() for n, d in overrides.items()}}
    a.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta))


if __name__ == '__main__':
    main()
