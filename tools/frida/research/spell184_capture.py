#!/usr/bin/env python3
"""Payoff184 Holy Light approach bounded live capture (owned spawn only) in research environments B/C.

Adapted from foot032_capture.py.  --mode observe attaches target021_observer.js (read-only hooks) to the
process this controller spawned; --mode control spawns/resumes/keys/kills only (observer-free control).
Both copy the probe's Preload file; a pre-existing file is preserved as <output>-previous-preload.txt.
Refuses the owner's environments (:94/27046 and :97/27048).  Run only through research/_env/live.sh.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
HERE = Path(__file__).resolve().parent


def owned_window(display, data, map_name):
    """Wine's X11 PID is a Linux PID, whereas Frida reports the Windows PID.

    Old crash reporters share Warcraft's title. Verify the invocation before
    focusing a window; neither title alone nor an arbitrary visible match owns it.
    """
    env = {**os.environ, 'DISPLAY': display}
    found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', '^Warcraft III$'],
                           env=env, capture_output=True, text=True, check=False, timeout=5)
    for window in found.stdout.splitlines():
        host = subprocess.run(['xdotool', 'getwindowpid', window], env=env,
                              capture_output=True, text=True, check=False, timeout=5)
        try:
            cmd = Path('/proc', str(int(host.stdout.strip())), 'cmdline').read_bytes()
        except (OSError, ValueError):
            continue
        argv = [part.decode('utf-8', 'replace').replace('\\', '/') for part in cmd.split(b'\0') if part]
        expected = str(data).replace('\\', '/').lower()
        if any(expected + '/war3.exe' in arg.lower() for arg in argv) and map_name.replace('\\', '/') in argv:
            return window, int(host.stdout.strip())
    return None


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--mode', choices=('observe', 'control'), required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--seconds', type=float, default=330)
    ap.add_argument('--continue-at', type=float, default=45)
    ap.add_argument('--prefix', default='S184 ')
    ap.add_argument('--preload', default='rs-spell184.txt')
    ap.add_argument('--task', default='payoff184')
    ap.add_argument('--extension', type=Path, help='additional read-only observer source')
    ap.add_argument('--always', action='store_true', help='observer records outside scene windows too')
    ap.add_argument('--lite', action='store_true', help='compact observer rows for crowd scenes')
    ap.add_argument('--output', type=Path, required=True, help='new JSONL path')
    args = ap.parse_args()
    if args.remote.endswith(':27046') or args.remote.endswith(':27048') or args.x11_display in (':94', ':97'):
        ap.error('refusing the owner environments')
    if args.output.exists():
        ap.error('output must be new')
    data = args.data.resolve()
    if 'w3-research' not in str(data) or str(data).endswith('w3-research'):
        ap.error('research install B/C only')
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = int.from_bytes(binary[0x3c:0x40], 'little')
    config = dict(timestamp=int.from_bytes(binary[pe + 8:pe + 12], 'little'),
                  imageSize=int.from_bytes(binary[pe + 80:pe + 84], 'little'), prefix=args.prefix, always=args.always, lite=args.lite)
    map_path = data / args.map.replace('\\', '/')
    sources = [Path(__file__), HERE / 'target021_observer.js', HERE / 'spell184_probe.j', HERE / 'spell184_make_map.py', HERE / 'target021_make_map.py']
    if args.extension:
        sources.append(args.extension)
    if (HERE / 'target03_probe.j').exists():
        sources.append(HERE / 'target03_probe.j')
    provenance = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    generated = data / 'CustomMapData' / args.preload
    args.output.parent.mkdir(parents=True, exist_ok=True)
    stem = args.output.with_suffix('')
    if generated.exists():
        Path(str(stem) + '-previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = session = None
    errors = []
    complete = ticking = False
    marker_re = re.compile(r'call Preload\( "(' + re.escape(args.prefix) + r'[^"\r\n]*)" \)')
    with args.output.open('w') as out:
        def record(row):
            out.write(json.dumps(row, separators=(',', ':')) + '\n')
            out.flush()

        def message(msg, _):
            nonlocal complete, ticking
            payload = msg['payload'] if msg['type'] == 'send' else msg
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))
            for row in (payload['rows'] if payload.get('event') == 'batch' else [payload]):
                record(row)
                if row.get('event') == 'marker':
                    v = row.get('value', '')
                    if ' label=complete' in v:
                        complete = True
                    if ' label=begin-setup' in v:
                        ticking = True
        try:
            pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record(dict(event='metadata', task=args.task, mode=args.mode, sha256=HASH, pid=pid, owned=True,
                        source_sha256=provenance, map=args.map, seconds=args.seconds, continue_at=args.continue_at,
                        display=args.x11_display, remote=args.remote, data=str(data), frida=frida.__version__, **config))
            script = None
            if args.mode == 'observe':
                session = device.attach(pid)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' +
                                               (HERE / 'target021_observer.js').read_text() + '\n' +
                                               (args.extension.read_text() if args.extension else ''))
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            keys = 0
            done_at = None
            while time.monotonic() - start < args.seconds and not errors:
                if (not ticking and done_at is None and keys < 6 and
                        time.monotonic() - start >= args.continue_at + 15 * keys):
                    window = owned_window(args.x11_display, data, args.map)
                    if window:
                        env = {**os.environ, 'DISPLAY': args.x11_display}
                        subprocess.run(['xdotool', 'windowfocus', '--sync', window[0], 'keydown', 'space'],
                                       check=True, timeout=5, env=env)
                        time.sleep(0.25)
                        subprocess.run(['xdotool', 'keyup', 'space'], check=True, timeout=5, env=env)
                        record(dict(event='loading-key', elapsed=time.monotonic() - start,
                                    window=window[0], host_pid=window[1]))
                    else:
                        record(dict(event='loading-key-skipped', elapsed=time.monotonic() - start,
                                    reason='No visible window with the owned data/map invocation'))
                    keys += 1
                if done_at is None and (complete or (args.mode == 'control' and generated.exists())):
                    done_at = time.monotonic()
                if done_at is not None and time.monotonic() - done_at > 3:
                    break
                time.sleep(0.1)
            if script is not None:
                record(dict(event='trace-end', **script.exports_sync.finish()))
            record(dict(event='controller-end', elapsed=time.monotonic() - start, complete=complete))
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
            preload_complete = False
            if generated.exists():
                raw = generated.read_bytes()
                Path(str(stem) + '-preload.txt').write_bytes(raw)
                found = marker_re.findall(raw.decode('utf-8', 'replace'))
                preload_complete = bool(found) and ' label=complete' in found[-1]
                record(dict(event='preload-file', sha256=hashlib.sha256(raw).hexdigest(), markers=len(found),
                            complete=preload_complete))
            else:
                record(dict(event='preload-file', missing=True))
        if not preload_complete or (args.mode == 'observe' and not complete):
            raise RuntimeError('Incomplete simulation markers: this run cannot certify retail behavior')


if __name__ == '__main__':
    main()
