#!/usr/bin/env python3
"""TARGET-03.1/03.2 bounded live capture (owned spawn only) in research environments B/C.

Copy of target021_capture.py that appends target03_vis_observer.js (visibility-query branch trace) to
target021_observer.js at a checked anchor.

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
    ap.add_argument('--prefix', default='T03 ')
    ap.add_argument('--preload', default='rs-target03.txt')
    ap.add_argument('--task', default='TARGET-03.2')
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
    sources = [Path(__file__), HERE / 'target021_observer.js', HERE / 'target03_vis_observer.js', HERE / 'target03_probe.j',
               HERE / 'target021_make_map.py']
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
                source = (HERE / 'target021_observer.js').read_text()
                anchor = '    const safe = (f, fallback) => {try {return f();} catch (e) {return {error: String(e), ...(fallback || {})};}};\n'
                if source.count(anchor) != 1:
                    raise RuntimeError('visibility extension anchor differs')
                source = source.replace(anchor, anchor + '    installTarget03Visibility(base, hook, (e, r) => out(e, r), counter);\n', 1)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' + source + '\n' +
                                               (HERE / 'target03_vis_observer.js').read_text())
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            keys = 0
            done_at = None
            while time.monotonic() - start < args.seconds and not errors:
                if (not ticking and done_at is None and keys < 6 and
                        time.monotonic() - start >= args.continue_at + 15 * keys):
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus',
                                    '--sync', 'key', 'space'], check=False, timeout=5,
                                   env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL)
                    record(dict(event='loading-key', elapsed=time.monotonic() - start))
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
            if generated.exists():
                raw = generated.read_bytes()
                Path(str(stem) + '-preload.txt').write_bytes(raw)
                found = marker_re.findall(raw.decode('utf-8', 'replace'))
                record(dict(event='preload-file', sha256=hashlib.sha256(raw).hexdigest(), markers=len(found),
                            complete=bool(found) and ' label=complete' in found[-1]))
            else:
                record(dict(event='preload-file', missing=True))


if __name__ == '__main__':
    main()
