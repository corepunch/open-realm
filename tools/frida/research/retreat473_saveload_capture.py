#!/usr/bin/env python3
"""Read-only GROUP-03.4.7.3 retreat473 retail UI save/load capture.

Runs the unchanged retreat473 map. When the public tick reaches --save-tick, the owned window
receives F10, s, the save name and Return (single-player menu pauses the game; the save is a
native retail save). The first pass continues uninterrupted to its complete marker. Then F10,
l, the first save-list entry and Load restore that save; the loading screen is continued and
the second pass runs to complete again. The observer (observe mode) never calls game code or
writes game memory; control mode attaches nothing and follows the Preload file's tick.
Use research/_env/live.sh to reserve B/C.
"""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
HERE = Path(__file__).resolve().parent
PREFIXES = {'B': '/home/lofcz/.local/share/open-realm/wine-pathfinding-research2',
            'C': '/home/lofcz/.local/share/open-realm/wine-pathfinding-research3'}


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--preload', default='retreat473.txt')
    ap.add_argument('--marker-prefix', default='R473 ')
    ap.add_argument('--observer', default='retreat473_observer.js')
    ap.add_argument('--mode', choices=('observe', 'control'), required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--env', choices=('B', 'C'), required=True)
    ap.add_argument('--seconds', type=float, default=400)
    ap.add_argument('--continue-at', type=float, default=15)
    ap.add_argument('--input-helper', type=Path, required=True)
    ap.add_argument('--save-tick', type=int, required=True)
    ap.add_argument('--save-name', required=True)
    ap.add_argument('--list-click', default='366,204')
    ap.add_argument('--load-click', default='353,414')
    ap.add_argument('--direct-keys', action='store_true', help='send keys to the window without refocusing each time')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    focused_keys = args.direct_keys
    if args.remote.split(':')[-1] in ('27046', '27048') or args.x11_display in (':94', ':97'):
        ap.error('refusing the owner environments')
    if {'B': ':98', 'C': ':99'}[args.env] != args.x11_display:
        ap.error('display/environment mismatch')
    if args.output.exists():
        ap.error('output must be new')
    if not re.fullmatch(r'[A-Z0-9]{3,16}', args.save_name):
        ap.error('plain save name')
    data = args.data.resolve()
    if 'w3-research2' not in str(data) and 'w3-research3' not in str(data):
        ap.error('research environment B/C install only')
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = int.from_bytes(binary[0x3c:0x40], 'little')
    config = dict(timestamp=int.from_bytes(binary[pe + 8:pe + 12], 'little'), imageSize=int.from_bytes(binary[pe + 80:pe + 84], 'little'),
                  prefix=args.marker_prefix)
    map_path = data / args.map.replace('\\', '/')
    sources = [Path(__file__), HERE / args.observer, HERE / 'retreat473_make_map.py', HERE / 'retreat473_probe.j',
               HERE / 'retreat473_probe.ai', args.input_helper, Path(str(args.input_helper) + '.so')]
    provenance = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    generated = data / 'CustomMapData' / args.preload
    save_file = data / 'save' / 'Profile1' / (args.save_name + '.w3z')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    stem = args.output.with_suffix('')
    removed = []
    if save_file.exists():
        removed.append(dict(file=save_file.name, sha256=hashlib.sha256(save_file.read_bytes()).hexdigest()))
        save_file.unlink()
    if generated.exists():
        Path(str(stem) + '-previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = session = None
    errors = []
    state = dict(tick=-1, completes=0, detached=False, markers=0)
    env = {**os.environ, 'DISPLAY': args.x11_display, 'WINEPREFIX': PREFIXES[args.env], 'WINEDEBUG': '-all'}
    tick_re = re.compile(re.escape(args.marker_prefix) + r'tick=(\d+) ')
    file_tick_re = re.compile(r'call Preload\( "' + re.escape(args.marker_prefix) + r'tick=(\d+) label=([a-z-]+)')
    with args.output.open('w', buffering=1) as out:
        def record(row):
            out.write(json.dumps(row) + '\n')

        def message(msg, _):
            payload = msg['payload'] if msg['type'] == 'send' else msg
            record(payload)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))
            if payload.get('event') == 'marker':
                v = payload.get('value', '')
                m = tick_re.match(v)
                if m:
                    state['tick'] = int(m.group(1))
                    state['markers'] += 1
                if ' label=complete' in v and 'label=unit' not in v:
                    state['completes'] += 1

        def window():
            wins = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III'], env=env,
                                  capture_output=True, text=True, timeout=10).stdout.split()
            return wins[0] if len(wins) == 1 else None

        def ui(kind, value=''):
            w = window()
            status = None
            if kind == 'shot':
                cmd = ['import', '-display', args.x11_display, '-window', 'root', str(stem) + f'-ui-{value}.png']
            elif w is None:
                cmd = None
            elif kind == 'key':
                cmd = ['xdotool', 'key', '--window', w, value] if focused_keys else ['xdotool', 'windowfocus', '--sync', w, 'key', value]
            elif kind == 'type':
                cmd = ['xdotool', 'windowfocus', '--sync', w, 'type', '--delay', '120', value]
            else:
                x, y = value.split(',')
                cmd = ['xdotool', 'mousemove', '--window', w, x, y, 'sleep', '0.3', 'mousedown', '1', 'sleep', '0.2', 'mouseup', '1']
            if cmd:
                status = subprocess.run(cmd, env=env, timeout=30, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
            record(dict(event='ui-action', kind=kind, value=value, window=w, status=status, tick=state['tick'],
                        elapsed=time.monotonic() - start))

        def file_progress():
            if not generated.exists():
                return
            text = generated.read_text(errors='replace')
            found = file_tick_re.findall(text)
            if found:
                tick = int(found[-1][0])
                if tick != state['tick']:
                    record(dict(event='file-tick', tick=tick, elapsed=time.monotonic() - start))
                    if state.get('first_file') is None:
                        state['first_file'] = (time.monotonic(), tick)
                state['tick'] = tick
            state['file_completes'] = sum(1 for _, label in found if label == 'complete')

        try:
            pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record(dict(event='metadata', task='GROUP-03.4.7.3/retreat473-saveload', mode=args.mode, sha256=HASH, pid=pid, owned=True,
                        source_sha256=provenance, map=args.map, seconds=args.seconds, continue_at=args.continue_at,
                        display=args.x11_display, remote=args.remote, env=args.env, save_tick=args.save_tick,
                        save_name=args.save_name, removed_before_launch=removed, frida=frida.__version__, **config))
            script = None
            if args.mode == 'observe':
                session = device.attach(pid)

                def detached(reason, crash):
                    state['detached'] = True
                    record(dict(event='owned-detached', reason=reason, crash=str(crash) if crash else None))
                session.on('detached', detached)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' + (HERE / args.observer).read_text())
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            keys = 0
            next_key = start + args.continue_at
            phase = 'first'
            phase_at = start
            focused = False
            load_markers = 0
            while time.monotonic() - start < args.seconds and not errors and not state['detached']:
                now = time.monotonic()
                if args.mode == 'control':
                    file_progress()
                if phase == 'first' and state['tick'] < 1 and keys < 10 and now >= next_key:
                    res = subprocess.run([str(args.input_helper.resolve()), str(pid), 'continue'],
                                         timeout=15, env=env, capture_output=True, text=True)
                    record(dict(event='loading-key', elapsed=now - start, rc=res.returncode, tick=state['tick']))
                    keys += 1
                    next_key = now + 8
                if phase == 'first' and not focused and state['tick'] >= args.save_tick - 40:
                    # Focus the owned window well before the save key; a focus change in the
                    # same xdotool call delayed the key by several game seconds (sl-explore-1).
                    w = window()
                    if w is not None:
                        rc = subprocess.run(['xdotool', 'windowfocus', '--sync', w], env=env, timeout=30,
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
                        record(dict(event='ui-focus', window=w, status=rc, tick=state['tick'], elapsed=now - start))
                    focused = True
                # Without hooks the Preload file can lag the simulation; the public timer runs
                # ten ticks per second, so the control also schedules from its first file tick.
                due = state['tick'] >= args.save_tick
                if args.mode == 'control' and state.get('first_file'):
                    t0, k0 = state['first_file']
                    due = due or now >= t0 + (args.save_tick - k0) / 10
                if phase == 'first' and not focused and args.mode == 'control' and state.get('first_file') and \
                        now >= state['first_file'][0] + (args.save_tick - 40 - state['first_file'][1]) / 10:
                    w = window()
                    if w is not None:
                        subprocess.run(['xdotool', 'windowfocus', '--sync', w], env=env, timeout=30,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        record(dict(event='ui-focus', window=w, tick=state['tick'], elapsed=now - start))
                    focused = True
                if phase == 'first' and due:
                    ui('key', 'F10')
                    # The menu pauses single-player simulation: wait until the public tick stops.
                    last, still = state['tick'], time.monotonic()
                    while time.monotonic() - still < 1.5 and time.monotonic() - start < args.seconds:
                        time.sleep(0.1)
                        if args.mode == 'control':
                            file_progress()
                        if state['tick'] != last:
                            last, still = state['tick'], time.monotonic()
                    record(dict(event='paused', tick=state['tick'], elapsed=time.monotonic() - start))
                    ui('shot', 'menu')
                    ui('key', 's'); time.sleep(1.5)
                    ui('shot', 'save-dialog')
                    ui('type', args.save_name); time.sleep(0.8)
                    ui('key', 'Return'); time.sleep(4)
                    ui('shot', 'after-save')
                    record(dict(event='save-done', exists=save_file.exists(),
                                sha256=hashlib.sha256(save_file.read_bytes()).hexdigest() if save_file.exists() else None))
                    phase, phase_at = 'saved', time.monotonic()
                done = state['completes'] if args.mode == 'observe' else state.get('file_completes', 0)
                if phase == 'saved' and done >= 1:
                    time.sleep(3)
                    ui('key', 'F10'); time.sleep(1.5)
                    ui('key', 'l'); time.sleep(2)
                    ui('shot', 'load-dialog')
                    ui('click', args.list_click); time.sleep(1)
                    ui('click', args.load_click); time.sleep(6)
                    ui('shot', 'loading')
                    phase, phase_at = 'loading', time.monotonic()
                    load_markers = state['markers']
                    next_key = phase_at
                    load_mtime = generated.stat().st_mtime if generated.exists() else 0
                if phase == 'loading':
                    resumed = state['markers'] > load_markers if args.mode == 'observe' else \
                        (generated.exists() and generated.stat().st_mtime > load_mtime)
                    if resumed:
                        record(dict(event='load-resumed', elapsed=now - start, tick=state['tick']))
                        ui('shot', 'resumed')
                        phase, phase_at = 'second', now
                    elif now >= next_key:
                        ui('key', 'space')
                        next_key = now + 3
                if phase == 'second':
                    if args.mode == 'observe':
                        finished = state['completes'] >= 2
                    else:
                        finished = state.get('file_completes', 0) >= 1 and state['tick'] == 300 and generated.stat().st_mtime > load_mtime
                    if finished:
                        time.sleep(3)
                        break
                time.sleep(0.05)
            record(dict(event='phase-end', phase=phase, elapsed=time.monotonic() - start, tick=state['tick']))
            if script is not None and not state['detached']:
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
            if save_file.exists():
                raw = save_file.read_bytes()
                Path(str(stem) + '-save.w3z').write_bytes(raw)
                record(dict(event='save-file', sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw)))
            if generated.exists():
                raw = generated.read_bytes()
                Path(str(stem) + '-preload.txt').write_bytes(raw)
                found = re.findall(r'call Preload\( "(' + re.escape(args.marker_prefix) + r'[^"\r\n]*)" \)', raw.decode('utf-8', 'replace'))
                record(dict(event='preload-file', sha256=hashlib.sha256(raw).hexdigest(), markers=len(found),
                            complete=any(' label=complete' in v for v in found)))
            else:
                record(dict(event='preload-file', missing=True))


if __name__ == '__main__':
    main()
