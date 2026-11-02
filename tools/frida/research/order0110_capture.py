#!/usr/bin/env python3
"""ORDER-01.10/01.18 bounded live capture (owned spawn only, isolated research environments B/C).

--mode observe : attach order0110_observer.js (read-only hooks), write JSONL.
--mode control : no attach/script/hooks; spawn/resume/keys/input/kill only.
Both modes copy the probe's Preload output (rs-<prefix>-<variant>.txt) next to the JSONL and preserve
any pre-existing file as <stem>-previous-preload.txt. Never attaches to foreign PIDs; refuses the
owner's environments. Optional genuine player input uses the owned Winelib helper
(order0110_ui_input.c): --input TICK X Y KEY [shift] triggered by the probe's observed tick marker
(observe) or by wall-clock seconds after the start marker file timestamp (control: --input-seconds).

Run via research/_env/live.sh, e.g.
  live.sh python3 order0110_capture.py --data {DATA} --remote {REMOTE} --x11-display {DISPLAY} \
     --map 'Maps\\RS-ORDER-01.10-a.w3m' --variant a --mode observe --output .../o110-a-1.jsonl
"""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
HERE = Path(__file__).resolve().parent
PROBES = ['order0110_probe.j', 'order0110_queue_probe.j', 'order0118_probe.j', 'order0110_queue_probe_b.j', 'order0118_probe_b.j']


def main():
    import frida
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--variant', required=True)
    ap.add_argument('--prefix', default='o110', help='Preload output prefix: rs-<prefix>-<variant>.txt')
    ap.add_argument('--mode', choices=('observe', 'control'), required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--seconds', type=float, default=150)
    ap.add_argument('--continue-at', type=float, default=30)
    ap.add_argument('--input', action='append', nargs='+', metavar='ARG',
                    help='TICK X Y KEY [shift]: owned helper input at probe tick (KEY in move,attack,patrol,none)')
    ap.add_argument('--input-helper', type=Path)
    ap.add_argument('--output', type=Path, required=True, help='new JSONL path')
    args = ap.parse_args()
    if args.remote.endswith(':27046') or args.remote.endswith(':27048') or args.x11_display in (':94', ':97'):
        ap.error('refusing the owner environments')
    if args.output.exists():
        ap.error('output must be new')
    plan = []
    for item in args.input or []:
        if len(item) not in (4, 5) or (len(item) == 5 and item[4] != 'shift') or item[3] not in ('move', 'attack', 'patrol', 'none'):
            ap.error('--input TICK X Y KEY [shift]')
        plan.append(dict(tick=int(item[0]), pixel=[int(item[1]), int(item[2])], key=item[3], shift=len(item) == 5))
    if plan and (not args.input_helper or not args.input_helper.is_file()):
        ap.error('--input requires the built owned helper')
    data = args.data.resolve()
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = int.from_bytes(binary[0x3c:0x40], 'little')
    config = dict(timestamp=int.from_bytes(binary[pe + 8:pe + 12], 'little'), imageSize=int.from_bytes(binary[pe + 80:pe + 84], 'little'))
    map_path = data / args.map.replace('\\', '/')
    sources = [Path(__file__), HERE / 'order0110_observer.js', HERE / 'order0110_make_map.py'] + [HERE / p for p in PROBES if (HERE / p).exists()]
    if args.input_helper:
        sources += [args.input_helper, HERE / 'order0110_ui_input.c']
    provenance = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    generated = data / 'CustomMapData' / f'rs-{args.prefix}-{args.variant}.txt'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    stem = args.output.with_suffix('')
    if generated.exists():
        Path(str(stem) + '-previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = session = None
    errors = []
    state = dict(complete=False, tick=-1, start=None)
    env = {**os.environ, 'DISPLAY': args.x11_display, 'WAYLAND_DISPLAY': '', 'WINEDEBUG': '-all'}
    prefixes = {'27049': 'wine-pathfinding-research2', '27050': 'wine-pathfinding-research3'}
    port = args.remote.rsplit(':', 1)[-1]
    if plan:
        if port not in prefixes:
            ap.error('input helper requires research environment B or C')
        env['WINEPREFIX'] = str(Path.home() / '.local/share/open-realm' / prefixes[port])
    with args.output.open('w') as out:
        def record(row):
            out.write(json.dumps(row) + '\n')

        def message(msg, _):
            payload = msg['payload'] if msg['type'] == 'send' else msg
            record(payload)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))
            if payload.get('event') == 'marker':
                v = payload.get('value', '')
                m = re.match(r'PATHTRACE tick=(\d+) ', v)
                if m:
                    state['tick'] = int(m.group(1))
                    if state['start'] is None and 'label=start_' in v:
                        state['start'] = time.monotonic()
                if v.startswith('PATHMETA complete'):
                    state['complete'] = True
        try:
            pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record(dict(event='metadata', task='ORDER-01.10', mode=args.mode, sha256=HASH, pid=pid, owned=True,
                        source_sha256=provenance, map=args.map, variant=args.variant, seconds=args.seconds,
                        continue_at=args.continue_at, display=args.x11_display, remote=args.remote, input=plan,
                        frida=frida.__version__, **config))
            script = None
            if args.mode == 'observe':
                session = device.attach(pid)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' + (HERE / 'order0110_observer.js').read_text())
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            keys = 0
            next_key = start + args.continue_at
            done_at = None
            sent = 0
            while time.monotonic() - start < args.seconds and not errors:
                # Loading needs one key to finish and (campaign-style) one key after the start marker to
                # begin game time. Observe: keys until the first probe tick. Control: fixed schedule.
                now = time.monotonic()
                if state['start'] is not None and state['tick'] < 1:
                    next_key = min(next_key, state['start'] + 3)
                want = (state['tick'] < 1 if args.mode == 'observe' else not generated.exists())
                if want and keys < 8 and now >= next_key:
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus', '--sync', 'key', 'space'],
                                   check=True, timeout=5, env=env, stdout=subprocess.DEVNULL)
                    record(dict(event='loading-key', elapsed=now - start, tick=state['tick']))
                    keys += 1
                    next_key = now + 10
                if sent < len(plan) and state['tick'] >= plan[sent]['tick']:
                    p = plan[sent]
                    cmd = [str(args.input_helper.resolve()), str(pid), *map(str, p['pixel'])] + (['shift'] if p['shift'] else []) + ([p['key']] if p['key'] != 'none' else [])
                    res = subprocess.run(cmd, env=env, timeout=10, capture_output=True, text=True)
                    record(dict(event='player-input', at_tick=state['tick'], plan=p, rc=res.returncode, stdout=res.stdout, stderr=res.stderr,
                                elapsed=time.monotonic() - start))
                    sent += 1
                if done_at is None and (state['complete'] or (args.mode == 'control' and generated.exists())):
                    done_at = time.monotonic()
                if done_at is not None and time.monotonic() - done_at > 3:
                    break
                time.sleep(0.05)
            if script is not None:
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
                    session.detach()
            if generated.exists():
                raw = generated.read_bytes()
                Path(str(stem) + '-preload.txt').write_bytes(raw)
                found = re.findall(r'call Preload\( "((?:O110|O118|PATH)[^"\r\n]*)" \)', raw.decode('utf-8', 'replace'))
                record(dict(event='preload-file', sha256=hashlib.sha256(raw).hexdigest(), markers=len(found),
                            complete=any(v.startswith('PATHMETA complete') for v in found)))
            else:
                record(dict(event='preload-file', missing=True))


if __name__ == '__main__':
    main()
