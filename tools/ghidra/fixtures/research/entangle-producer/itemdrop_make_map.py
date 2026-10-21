#!/usr/bin/env python3
"""Build the stock or authored-DropItemRange point-drop probe map; no engine writes."""
import argparse, hashlib, json, subprocess, tempfile, struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
RANGES = {'stock': 150, 'authored': 64, 'zero': 0}


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
    stem = 'target-entangle-v2-' + a.setting
    script = (HERE / 'itemdrop_probe.j').read_text().replace('DROP_RANGE', '%d.0' % RANGES[a.setting])
    script = script.replace('PICKUP_RANGE', str(RANGES[a.setting])+'.0')
    script = script.replace('PRELOAD_FILE', stem + '.txt').replace('START_FILE', stem + '-start.txt')
    script += ('\nfunction config takes nothing returns nothing\n call SetPlayers(1)\n call SetTeams(1)\n'
               ' call SetPlayerController(Player(0),MAP_CONTROL_USER)\n'
               ' call SetPlayerSlotAvailable(Player(0),MAP_CONTROL_USER)\nendfunction\n'
               'function main takes nothing returns nothing\n call InitBlizzard()\n call PathProbeInit()\nendfunction\n')
    # Empty object-ability table: the arena keeps stock Inventory/Move rows.
    overrides = {'war3map.j': script.encode(), 'war3map.w3a': struct.pack('<III',2,0,0)}
    overrides['war3mapMisc.txt'] = ('[Misc]\nDropItemRange=100\nGiveItemRange=150\nPawnItemRange=130\nPickupItemRange=%d\n' % RANGES[a.setting]).encode()
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
    meta = {'setting': a.setting, 'pickup_range': RANGES[a.setting], 'base_sha256': hashlib.sha256(a.base.read_bytes()).hexdigest(),
            'map_sha256': hashlib.sha256(a.output.read_bytes()).hexdigest(),
            'sources': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in [Path(__file__), HERE / 'itemdrop_probe.j']},
            'overrides': {n: hashlib.sha256(d).hexdigest() for n, d in overrides.items()}}
    a.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta))


if __name__ == '__main__':
    main()
