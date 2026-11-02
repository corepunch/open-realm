#!/usr/bin/env python3
"""SEP-03/MAP-05/MAP-06 bounded owned-spawn capture (isolated research environment only).

Modes:
  observe  - spawn war3.exe, inject sep03_map06_observer.js (read-only hooks), resume.
  control  - spawn/resume/kill only: no attach, no script, no hooks; JASS Preload output only.
Sends Space on the owned X display at --continue-at and every --continue-every seconds
afterwards (reload/load screens), then kills only the spawned process. Pattern copied from
trace_wc3_pathfinding.py / control_wc3_pathfinding.py (not edited).
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
DEFAULT_CAPS = {'default': 200000, 'sobj-update': 400000, 'mover-commit': 200000, 'snapshot': 400}


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mode', choices=('observe', 'control'), required=True)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--seconds', type=float, default=150)
    ap.add_argument('--continue-at', type=float, default=80)
    ap.add_argument('--continue-every', type=float, default=0)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--snapshot-labels', default='snapshot_inserted,before_move,snapshot_before_remove,snapshot_removed,burst_moved,complete,snapshot_created,after_move,before_save,after_save,load_armed,before_load,resumed_after_load,before_restart,before_changelevel')
    ap.add_argument('--snapshot-every', type=int, default=0)
    ap.add_argument('--all-updates', action='store_true')
    ap.add_argument('--all-movers', action='store_true')
    ap.add_argument('--observer-extra', type=Path, help='additional read-only observer source, recorded in provenance')
    ap.add_argument('--delete-save', action='append', default=[], help='RS-* save name to remove before launch')
    ap.add_argument('--preload-names', default='', help='comma list of CustomMapData rs-*.txt files to collect')
    ap.add_argument('--screenshot-every', type=float, default=0, help='PNG of the owned display every N s after --continue-at')
    ap.add_argument('--ui-action', action='append', default=[],
                    help='AT:key:KEYSYM | AT:type:TEXT | AT:click:X,Y (window coordinates) | AT:shot; AT seconds after spawn')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.remote.endswith(':27046') or args.x11_display == ':94' or 'w3-research' not in str(args.data):
        ap.error('refusing the owner environment')
    if not 0 < args.continue_at < args.seconds <= 600:
        ap.error('bounded run required')
    if args.output.exists():
        ap.error('output must be new')
    data = args.data.resolve()
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    here = Path(__file__).resolve().parent
    js = here / 'sep03_map06_observer.js'
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    map_path = data / args.map.replace('\\', '/')
    provenance = {'sep03_map06_trace.py': sha(Path(__file__)), 'sep03_map06_observer.js': sha(js),
                  'sep03_map06_probe.j': sha(here / 'sep03_map06_probe.j'), 'map': sha(map_path)}
    extra = args.observer_extra.read_text() if args.observer_extra else ''
    if args.observer_extra:
        provenance[str(args.observer_extra)] = sha(args.observer_extra)
    saves = data / 'save' / 'Profile1'
    removed = []
    for name in args.delete_save:
        if not name.startswith('RS'):
            ap.error('only RS* research saves may be removed')
        for p in sorted(saves.rglob(name + '*')):
            if p.is_file() and p.name.startswith('RS'):
                removed.append(str(p.relative_to(saves)))
                p.unlink()
    preload_names = [n for n in args.preload_names.split(',') if n]
    for n in preload_names:
        if not n.startswith('rs-'):
            ap.error('only rs-* preload files')
        p = data / 'CustomMapData' / n
        if p.exists():
            p.unlink()
            removed.append('CustomMapData/' + n)
    config = dict(timestamp=struct.unpack_from('<I', binary, pe + 8)[0], imageSize=struct.unpack_from('<I', binary, pe + 80)[0],
                  caps=DEFAULT_CAPS, snapshotLabels=[s for s in args.snapshot_labels.split(',') if s],
                  snapshotEvery=args.snapshot_every, allUpdates=args.all_updates, allMovers=args.all_movers)
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    lock = threading.Lock()
    errors = []
    pid = session = script = None
    args.output.parent.mkdir(parents=True, exist_ok=True)
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
            record({'event': 'metadata', 'sha256': HASH, 'pid': pid, 'owned': True, 'mode': args.mode,
                    'source_sha256': provenance, 'map': args.map, 'seconds': args.seconds, 'continue_at': args.continue_at,
                    'continue_every': args.continue_every, 'display': args.x11_display, 'remote': args.remote,
                    'removed_before_launch': removed, 'frida': frida.__version__, 'config': config})
            if args.mode == 'observe':
                session = device.attach(pid)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' + js.read_text() + '\n' + extra)
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            next_key = args.continue_at
            next_shot = args.continue_at
            ui_plan = sorted((float(a.split(':', 2)[0]), a.split(':', 2)[1], a.split(':', 2)[2] if a.count(':') >= 2 else '')
                             for a in args.ui_action)
            while time.monotonic() - start < args.seconds:
                if len(errors) > 20:
                    break
                if next_key is not None and time.monotonic() - start >= next_key:
                    r = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus',
                                        '--sync', 'key', 'space'], timeout=10,
                                       env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
                    record({'event': 'loading-key', 'elapsed': time.monotonic() - start, 'status': r.returncode})
                    next_key = next_key + args.continue_every if args.continue_every > 0 else None
                while ui_plan and time.monotonic() - start >= ui_plan[0][0]:
                    at, kind, value = ui_plan.pop(0)
                    env = {**os.environ, 'DISPLAY': args.x11_display}
                    wins = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III'], env=env,
                                          capture_output=True, text=True, timeout=10).stdout.split()
                    status = None
                    if len(wins) == 1:
                        if kind == 'key':
                            cmd = ['xdotool', 'windowfocus', '--sync', wins[0], 'key', value]
                        elif kind == 'type':
                            cmd = ['xdotool', 'windowfocus', '--sync', wins[0], 'type', '--delay', '120', value]
                        elif kind == 'click':
                            x, y = value.split(',')
                            cmd = ['xdotool', 'mousemove', '--window', wins[0], x, y, 'sleep', '0.3', 'mousedown', '1', 'sleep', '0.2', 'mouseup', '1']
                        else:
                            cmd = ['import', '-display', args.x11_display, '-window', 'root',
                                   str(args.output.parent / f'{args.output.stem}-ui-{int(at):03d}.png')]
                        status = subprocess.run(cmd, env=env, timeout=30, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
                    record({'event': 'ui-action', 'at': at, 'elapsed': time.monotonic() - start, 'kind': kind, 'value': value,
                            'windows': len(wins), 'status': status})
                if args.screenshot_every > 0 and time.monotonic() - start >= next_shot:
                    shot = args.output.parent / f'{args.output.stem}-shot-{int(time.monotonic() - start):03d}.png'
                    r = subprocess.run(['import', '-display', args.x11_display, '-window', 'root', str(shot)], timeout=20,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    record({'event': 'screenshot', 'elapsed': time.monotonic() - start, 'file': shot.name, 'status': r.returncode})
                    next_shot += args.screenshot_every
                time.sleep(0.1)
            if script is not None:
                record({'event': 'trace-end', 'elapsed': time.monotonic() - start, **script.exports_sync.finish()})
            else:
                record({'event': 'control-end', 'elapsed': time.monotonic() - start})
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
            collected = {}
            for n in preload_names:
                p = data / 'CustomMapData' / n
                if p.exists():
                    raw = p.read_bytes()
                    (args.output.parent / (args.output.stem + '-' + n)).write_bytes(raw)
                    collected[n] = hashlib.sha256(raw).hexdigest()
            created = sorted((str(p.relative_to(saves)), hashlib.sha256(p.read_bytes()).hexdigest()) for p in saves.rglob('RS*') if p.is_file())
            record({'event': 'artifacts', 'preload_sha256': collected, 'saves_after': created, 'errors': errors[:20]})


if __name__ == '__main__':
    main()
