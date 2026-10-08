#!/usr/bin/env python3
"""Bounded NUM-04.5/NUM-04.6 startup random-state capture/control for WC3 1.27.1.7085 (research environments B/C only).

Modes:
  observe  spawn war3.exe -window -loadfile <map>, attach tools/frida/research/NUM-04.5_rng_observer.js (read-only)
           before resume, run --seconds, finish, kill the owned process; keep the map's JASS Preload output.
  control  spawn/resume/kill only (no attach, no script, no hooks); keeps the JASS Preload output.
Sends Space on the owned display at --key-at times (loading screen). Never attaches to a foreign PID.
Run through research/_env/live.sh with {DATA} {REMOTE} {DISPLAY} placeholders.
"""
import argparse, hashlib, json, os, re, struct, subprocess, sys, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
ALLOWED = {'127.0.0.1:27049': ':98', '127.0.0.1:27050': ':99'}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode', choices=('observe', 'control'))
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True, help=r'Maps\<name>.w3m')
    ap.add_argument('--preload-name', help='CustomMapData file written by PreloadGenEnd (optional)')
    ap.add_argument('--marker-prefix', action='append', default=[], help='Preload string prefixes to record as markers')
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--seconds', type=float, required=True)
    ap.add_argument('--key-at', type=float, nargs='*', default=[14.0, 22.0], help='wall seconds for Space keys')
    ap.add_argument('--detail-cap', type=int, default=150000)
    ap.add_argument('--sep-samples', type=int, default=3)
    ap.add_argument('--output', type=Path, required=True, help='new directory')
    a = ap.parse_args()
    if ALLOWED.get(a.remote) != a.x11_display or not re.search(r'w3-research[23]$', str(a.data)):
        ap.error('research environments B/C only (27049/:98/w3-research2, 27050/:99/w3-research3)')
    if not 0 < a.seconds <= 180:
        ap.error('seconds must be in (0,180]')
    data = a.data.resolve()
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    map_path = data / a.map.replace('\\', '/')
    a.output.mkdir(parents=True, exist_ok=False)
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = dict(timestamp=struct.unpack_from('<I', binary, pe + 8)[0], imageSize=struct.unpack_from('<I', binary, pe + 80)[0],
                  markerPrefixes=a.marker_prefix, detailCap=a.detail_cap, sepSamples=a.sep_samples)
    js = Path(__file__).with_name('NUM-04.5_rng_observer.js')
    prov = dict(mode=a.mode, binary_sha256=HASH, map=a.map, map_sha256=sha(map_path), controller_sha256=sha(__file__),
                observer_sha256=sha(js) if a.mode == 'observe' else None, frida=frida.__version__, seconds=a.seconds,
                key_at=a.key_at, display=a.x11_display, remote=a.remote, data=str(data), config=config, argv=sys.argv,
                started=time.strftime('%Y-%m-%dT%H:%M:%S'))
    generated = data / 'CustomMapData' / a.preload_name if a.preload_name else None
    if generated is not None and generated.exists():
        (a.output / 'previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    device = frida.get_device_manager().add_remote_device(a.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = session = script = None
    errors = []
    out = (a.output / 'capture.jsonl').open('w') if a.mode == 'observe' else None
    nrows = 0

    def write(row):
        nonlocal nrows
        out.write(json.dumps(row) + '\n'); nrows += 1

    def on_message(msg, _):
        if msg['type'] == 'send' and msg['payload'].get('event') == 'batch':
            for row in msg['payload']['rows']:
                write(row)
            return
        if msg['type'] == 'error':
            errors.append(msg.get('description', str(msg)))
        write(msg['payload'] if msg['type'] == 'send' else {'event': 'script-error', **msg})
    try:
        pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', a.map], cwd=path)
        prov['pid'] = pid
        if a.mode == 'observe':
            write(dict(event='metadata', sha256=HASH, owned=True, **prov))
            session = device.attach(pid)
            script = session.create_script('const config = ' + json.dumps(config) + ';\n' + js.read_text())
            script.on('message', on_message)
            script.load()
        device.resume(pid)
        start = time.monotonic()
        keys = sorted(a.key_at)
        while time.monotonic() - start < a.seconds and not errors:
            if keys and time.monotonic() - start >= keys[0]:
                keys.pop(0)
                subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus', '--sync', 'key', 'space'],
                               timeout=5, env={**os.environ, 'DISPLAY': a.x11_display}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if out:
                    write(dict(event='loading-key', elapsed=time.monotonic() - start))
            time.sleep(0.1)
        if script is not None:
            res = script.exports_sync.finish()
            time.sleep(0.5)
            write(dict(event='trace-end', **res))
        if errors:
            raise RuntimeError('; '.join(errors))
    except Exception as e:
        prov['error'] = str(e)
        if out:
            write(dict(event='trace-failed', error=str(e)))
        raise
    finally:
        if pid is not None:
            try:
                device.kill(pid)
            except frida.ProcessNotFoundError:
                prov['already_exited'] = True
        if session is not None:
            try:
                session.detach()
            except Exception:
                pass
        if out:
            out.close()
            prov['capture_sha256'] = sha(a.output / 'capture.jsonl')
            prov['rows'] = nrows
        if generated is not None:
            if generated.exists():
                raw = generated.read_bytes()
                (a.output / 'preload.txt').write_bytes(raw)
                prov['preload_sha256'] = hashlib.sha256(raw).hexdigest()
            else:
                prov['preload_missing'] = True
        prov['ended'] = time.strftime('%Y-%m-%dT%H:%M:%S')
        (a.output / 'provenance.json').write_text(json.dumps(prov, indent=1) + '\n')
        print(json.dumps({k: prov.get(k) for k in ('mode', 'map_sha256', 'capture_sha256', 'rows', 'preload_sha256', 'error')}))


if __name__ == '__main__':
    main()
