#!/usr/bin/env python3
"""BASE-02.1 bounded owned-spawn observer (isolated research env only).

Spawns war3.exe through the given Frida server, injects base021_observer.js (read-only
entry/exit hooks), sends one loading-screen Space on the given X display, and kills
only the spawned process. Pattern copied from trace_wc3_pathfinding.py (not edited).
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import threading
import time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--seconds', type=float, default=130)
    ap.add_argument('--continue-at', type=float, default=80)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--samples', type=int, default=4000)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.remote.endswith(':27046') or args.x11_display == ':94':
        ap.error('refusing the owner environment')
    if not 0 < args.continue_at < args.seconds <= 600:
        ap.error('bounded run required')
    if args.output.exists():
        ap.error('output must be new')
    binary = (args.data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = dict(timestamp=struct.unpack_from('<I', binary, pe + 8)[0],
                  imageSize=struct.unpack_from('<I', binary, pe + 80)[0], samples=args.samples)
    here = Path(__file__).resolve().parent
    js = here / 'base021_observer.js'
    map_path = args.data / args.map.replace('\\', '/')
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    provenance = {'base021_trace.py': sha(Path(__file__)), 'base021_observer.js': sha(js),
                  'base021_probe.j': sha(here / 'base021_probe.j'), 'map': sha(map_path)}
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(args.data.resolve()).replace('/', '\\')
    lock = threading.Lock()
    errors = []
    pid = session = None
    with args.output.open('w') as out:
        def record(row):
            with lock:
                out.write(json.dumps(row) + '\n')

        def message(msg, _):
            payload = msg['payload'] if msg['type'] == 'send' else msg
            record(payload)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))
        try:
            pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record({'event': 'metadata', 'sha256': HASH, 'pid': pid, 'owned': True, 'source_sha256': provenance,
                    'map': args.map, 'seconds': args.seconds, 'continue_at': args.continue_at,
                    'display': args.x11_display, 'remote': args.remote, 'frida': frida.__version__, **config})
            session = device.attach(pid)
            script = session.create_script('const config = ' + json.dumps(config) + ';\n' + js.read_text())
            script.on('message', message)
            script.load()
            device.resume(pid)
            start = time.monotonic()
            sent = False
            while time.monotonic() - start < args.seconds and not errors:
                if not sent and time.monotonic() - start >= args.continue_at:
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus',
                                    '--sync', 'key', 'space'], check=True, timeout=5,
                                   env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL)
                    record({'event': 'loading-key', 'elapsed': time.monotonic() - start})
                    sent = True
                time.sleep(0.1)
            record({'event': 'trace-end', **script.exports_sync.finish()})
            if errors:
                raise RuntimeError('; '.join(errors))
        except Exception as error:
            record({'event': 'trace-failed', 'error': str(error)})
            raise
        finally:
            try:
                if pid is not None:
                    try:
                        device.kill(pid)
                    except frida.ProcessNotFoundError:
                        record({'event': 'owned-process-already-exited', 'pid': pid})
            finally:
                if session is not None:
                    try:
                        session.detach()
                    except Exception:
                        pass


if __name__ == '__main__':
    main()
