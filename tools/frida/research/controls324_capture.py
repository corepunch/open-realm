#!/usr/bin/env python3
"""Repeat a frozen E2E-02.3 map in B/C without attaching or injecting a script."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def markers(raw, prefix):
    return re.findall(r'call Preload\( "(' + re.escape(prefix) + r'[^"\r\n]*)" \)', raw)


def main():
    import frida
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--scene', required=True)
    parser.add_argument('--env', choices=('B', 'C'), required=True)
    parser.add_argument('--input-helper', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    index = {'B': 2, 'C': 3}[args.env]
    data = Path('/run/media/lofcz/ssd_external/Games/w3-research' + str(index))
    if args.data.resolve() != data or digest(data / 'game.dll') != HASH:
        parser.error('isolated B/C installation and supported binary required')
    reference = json.loads(args.reference.read_text())[args.scene]
    if digest(data / reference['map']) != reference['map_sha256']:
        parser.error('frozen map differs')
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / 'controller.py').write_bytes(Path(__file__).read_bytes())
    (args.output / 'reference.json').write_bytes(args.reference.read_bytes())
    preload = data / 'CustomMapData' / reference['preload']
    if preload.exists():
        (args.output / 'previous-preload.txt').write_bytes(preload.read_bytes())
        preload.unlink()
    display = ':' + str(96 + index)
    remote = '127.0.0.1:' + str(27047 + index)
    prefix = '/home/lofcz/.local/share/open-realm/wine-pathfinding-research' + str(index)
    env = dict(os.environ, DISPLAY=display, WINEPREFIX=prefix, WINEDEBUG='-all')
    provenance = dict(task='E2E-02.3', scene=args.scene, mode='control',
                      transport='Frida spawn/resume/kill only; no attach, script, or hooks',
                      binary_sha256=HASH, map=reference['map'], map_sha256=reference['map_sha256'],
                      controller_sha256=digest(Path(__file__)), reference_sha256=digest(args.reference),
                      helper_sha256=digest(args.input_helper), helper_so_sha256=digest(Path(str(args.input_helper) + '.so')),
                      env=args.env, display=display, remote=remote, wine_prefix=prefix,
                      seconds=180, continue_at=30, loading_input=[], frida=frida.__version__)
    device = frida.get_device_manager().add_remote_device(remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = None
    try:
        pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', reference['map']], cwd=path)
        provenance['pid'] = pid
        device.resume(pid)
        start = time.monotonic()
        next_key = 30
        while time.monotonic() - start < 180:
            elapsed = time.monotonic() - start
            if preload.exists() and ' label=complete' in preload.read_text(errors='replace'):
                break
            if elapsed >= next_key and next_key < 110:
                result = subprocess.run([str(args.input_helper.resolve()), str(pid), 'continue'],
                                        timeout=5, env=env, capture_output=True, text=True)
                provenance['loading_input'].append(dict(pid=pid, elapsed=elapsed, rc=result.returncode,
                                                        stdout=result.stdout, stderr=result.stderr))
                if result.returncode:
                    raise RuntimeError('owned loading input failed')
                next_key += 8
            time.sleep(0.2)
        provenance['elapsed'] = time.monotonic() - start
        raw = preload.read_bytes()
        (args.output / 'preload.txt').write_bytes(raw)
        actual = markers(raw.decode('utf-8'), reference['prefix'])
        provenance.update(preload_sha256=digest(args.output / 'preload.txt'), markers=len(actual),
                          complete=bool(actual) and ' label=complete' in actual[-1],
                          matches=actual == reference['public'])
        if not provenance['complete'] or not provenance['matches']:
            raise RuntimeError('incomplete or different public trajectory; output retained')
    except Exception as error:
        provenance['error'] = str(error)
        raise
    finally:
        if pid is not None:
            try:
                device.kill(pid)
            except frida.ProcessNotFoundError:
                provenance['already_exited'] = True
        (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print(json.dumps(provenance))


if __name__ == '__main__':
    main()
