#!/usr/bin/env python3
"""MAP-04.1/04.2 bounded live capture in the isolated research environment.

Spawns an owned war3.exe (never attaches to a foreign PID), optionally loads the
read-only exclusion-scope observer (map04_2_scope_observer.js), presses the
loading key on the owned X display, and kills only the spawned process.
--control runs the same timeline without attaching/injecting anything.
The scenario's JASS Preload output is copied when the game rewrote it.
"""
import argparse, hashlib, json, os, re, struct, subprocess, sys, threading, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FORBIDDEN_REMOTES = {'127.0.0.1:27046'}
FORBIDDEN_DISPLAYS = {':94'}


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--scenario', required=True, help='Preload scenario name (CustomMapData/pathtrace-<scenario>.txt)')
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--seconds', type=float, required=True)
    ap.add_argument('--continue-at', type=float, default=80)
    ap.add_argument('--continue-after-start', type=float, default=5)
    ap.add_argument('--limit', type=int, default=600)
    ap.add_argument('--margin', type=int, default=1, help='extra hierarchy cells around snapshotted coverage')
    ap.add_argument('--control', action='store_true', help='no attach, no script, no hooks')
    ap.add_argument('--output', type=Path, required=True, help='new capture directory')
    a = ap.parse_args()
    if a.remote in FORBIDDEN_REMOTES or a.x11_display in FORBIDDEN_DISPLAYS or 'w3-research' not in str(a.data):
        ap.error('isolated research environment only')
    if not 0 < a.continue_at < a.seconds <= 600 or not re.fullmatch(r'[a-z0-9_]+', a.scenario):
        ap.error('bounded capture required')
    data = a.data.resolve()
    digest = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    map_path = (data / a.map.replace('\\', '/')).resolve()
    if not map_path.is_file() or not map_path.is_relative_to(data):
        ap.error('map must exist inside the data directory')
    a.output.mkdir(parents=True, exist_ok=False)
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = dict(timestamp=struct.unpack_from('<I', binary, pe + 8)[0], imageSize=struct.unpack_from('<I', binary, pe + 80)[0],
                  limit=a.limit, margin=a.margin)
    js = Path(__file__).with_name('map04_2_scope_observer.js')
    generated = data / 'CustomMapData' / ('pathtrace-' + a.scenario + '.txt')
    before = (generated.stat().st_mtime_ns, digest(generated)) if generated.exists() else None
    meta = dict(event='metadata', binary_sha256=HASH, map=a.map, map_sha256=digest(map_path), scenario=a.scenario,
                mode='control' if a.control else 'observe', seconds=a.seconds, continue_at=a.continue_at,
                continue_after_start=a.continue_after_start, remote=a.remote, display=a.x11_display, frida=frida.__version__,
                controller_sha256=digest(__file__), observer_sha256=None if a.control else digest(js), config=config,
                preload_before=before)
    device = frida.get_device_manager().add_remote_device(a.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    out = (a.output / 'capture.jsonl').open('w')
    lock = threading.Lock()
    errors, state = [], dict(start_marker=None)

    def record(row):
        with lock:
            out.write(json.dumps(row) + '\n')

    def on_message(msg, _):
        payload = msg['payload'] if msg['type'] == 'send' else msg
        record(payload)
        if msg['type'] == 'error':
            errors.append(msg.get('description', str(msg)))
        if payload.get('event') == 'marker' and 'label=start_' in payload.get('value', '') and state['start_marker'] is None:
            state['start_marker'] = time.monotonic()

    record(meta)
    (a.output / Path(__file__).name).write_bytes(Path(__file__).read_bytes())
    if not a.control:
        (a.output / js.name).write_bytes(js.read_bytes())
    pid = session = None
    try:
        pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', a.map], cwd=path)
        record(dict(event='spawned', pid=pid))
        if not a.control:
            session = device.attach(pid)
            script = session.create_script('const config = ' + json.dumps(config) + ';\n' + js.read_text())
            script.on('message', on_message)
            script.load()
        device.resume(pid)
        start = time.monotonic()
        keys = 0
        env = {**os.environ, 'DISPLAY': a.x11_display}
        while time.monotonic() - start < a.seconds and not errors:
            now = time.monotonic()
            due_fixed = keys == 0 and now - start >= a.continue_at
            due_second = keys == 1 and (now - start >= a.continue_at + a.continue_after_start if a.control or state['start_marker'] is None
                                        else now - state['start_marker'] >= a.continue_after_start)
            if due_fixed or due_second:
                subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus', '--sync', 'key', 'space'],
                               check=True, timeout=5, env=env, stdout=subprocess.DEVNULL)
                keys += 1
                record(dict(event='loading-key', elapsed=now - start, index=keys))
            time.sleep(0.1)
        if session is not None:
            record(dict(event='trace-end', **script.exports_sync.finish()))
        if errors:
            raise RuntimeError('; '.join(errors))
    except Exception as error:
        record(dict(event='trace-failed', error=str(error)))
        raise
    finally:
        try:
            if pid is not None:
                try:
                    device.kill(pid)
                except frida.ProcessNotFoundError:
                    record(dict(event='owned-process-already-exited', pid=pid))
        finally:
            if session is not None:
                try:
                    session.detach()
                except Exception:
                    pass
            after = (generated.stat().st_mtime_ns, digest(generated)) if generated.exists() else None
            fresh = after is not None and (before is None or after[0] != before[0])
            if fresh:
                (a.output / 'preload.txt').write_bytes(generated.read_bytes())
            record(dict(event='preload', fresh=fresh, after=after))
            out.close()


if __name__ == '__main__':
    main()
