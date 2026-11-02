#!/usr/bin/env python3
"""Bounded SEP research capture/control for WC3 1.27.1.7085 in the isolated research environment.

Modes:
  observe  spawn war3.exe, attach tools/frida/research/sep_research_observer.js (read-only), resume, run
           --seconds, finish, kill the owned process; also keeps the map's JASS Preload output.
  control  spawn/resume/kill only (no attach, no script, no hooks); keeps the JASS Preload output.
Both send Space on the owned display once the probe has started (loading screen), mirroring the
existing trace tool. Never attaches to a foreign PID. Wrap every launch with the research live lock.
"""
import argparse, hashlib, json, os, re, struct, subprocess, sys, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def preload_markers(raw):
    return re.findall(r'call Preload\( "(PATHSEP[^"\r\n]*)" \)', raw)


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode', choices=('observe', 'control'))
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True, help=r'Maps\RS-<TASK>-<variant>.w3m')
    ap.add_argument('--preload-name', required=True, help='CustomMapData file written by PreloadGenEnd')
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--seconds', type=float, required=True)
    ap.add_argument('--key-at', type=float, nargs='+', default=[14.0, 22.0], help='wall seconds for Space keys')
    ap.add_argument('--pair-probes', action='store_true')
    ap.add_argument('--retry-events', action='store_true')
    ap.add_argument('--observer-extra', type=Path, help='additional read-only observer, recorded in provenance')
    ap.add_argument('--output', type=Path, required=True, help='new directory')
    a = ap.parse_args()
    if a.remote.endswith(':27046') or a.x11_display == ':94' or 'w3-research' not in str(a.data):
        ap.error('research environment only (port 27048, display :97, Games/w3-research)')
    if not 0 < a.seconds <= 240:
        ap.error('seconds must be in (0,240]')
    data = a.data.resolve()
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    map_path = data / a.map.replace('\\', '/')
    a.output.mkdir(parents=True, exist_ok=False)
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = dict(timestamp=struct.unpack_from('<I', binary, pe + 8)[0], imageSize=struct.unpack_from('<I', binary, pe + 80)[0],
                  pairProbes=a.pair_probes, retryEvents=a.retry_events)
    js = Path(__file__).with_name('sep_research_observer.js')
    prov = dict(mode=a.mode, binary_sha256=HASH, map=a.map, map_sha256=sha(map_path), controller_sha256=sha(__file__),
                observer_sha256=sha(js) if a.mode == 'observe' else None, frida=frida.__version__, seconds=a.seconds,
                key_at=a.key_at, display=a.x11_display, remote=a.remote, config=config, argv=sys.argv,
                started=time.strftime('%Y-%m-%dT%H:%M:%S'))
    if a.observer_extra:
        prov['observer_extra_sha256'] = sha(a.observer_extra)
    generated = data / 'CustomMapData' / a.preload_name
    if generated.exists():
        (a.output / 'previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    device = frida.get_device_manager().add_remote_device(a.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = session = script = None
    errors = []
    out = (a.output / 'capture.jsonl').open('w') if a.mode == 'observe' else None
    nrows = 0

    def on_message(msg, _):
        nonlocal nrows
        payload = msg['payload'] if msg['type'] == 'send' else {'event': 'script-error', **msg}
        if msg['type'] == 'error':
            errors.append(msg.get('description', str(msg)))
        out.write(json.dumps(payload) + '\n'); nrows += 1
        if nrows % 256 == 0:
            out.flush()
    try:
        pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', a.map], cwd=path)
        prov['pid'] = pid
        if a.mode == 'observe':
            out.write(json.dumps(dict(event='metadata', sha256=HASH, owned=True, **prov)) + '\n')
            session = device.attach(pid)
            script = session.create_script('const config = ' + json.dumps(config) + ';\n' + js.read_text() + ('\n' + a.observer_extra.read_text() if a.observer_extra else ''))
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
                    out.write(json.dumps(dict(event='loading-key', elapsed=time.monotonic() - start)) + '\n')
            time.sleep(0.1)
        if script is not None:
            out.write(json.dumps(dict(event='trace-end', **script.exports_sync.finish())) + '\n')
        if errors:
            raise RuntimeError('; '.join(errors))
    except Exception as e:
        prov['error'] = str(e)
        if out:
            out.write(json.dumps(dict(event='trace-failed', error=str(e))) + '\n')
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
        if generated.exists():
            raw = generated.read_bytes()
            (a.output / 'preload.txt').write_bytes(raw)
            m = preload_markers(raw.decode('utf-8', 'replace'))
            prov.update(preload_sha256=hashlib.sha256(raw).hexdigest(), preload_markers=len(m),
                        preload_complete=bool(m) and ' label=complete' in m[-1])
        else:
            prov['preload_missing'] = True
        prov['ended'] = time.strftime('%Y-%m-%dT%H:%M:%S')
        (a.output / 'provenance.json').write_text(json.dumps(prov, indent=1) + '\n')
        print(json.dumps({k: prov.get(k) for k in ('mode', 'map_sha256', 'capture_sha256', 'preload_markers', 'preload_complete', 'error')}))


if __name__ == '__main__':
    main()
