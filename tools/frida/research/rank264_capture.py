#!/usr/bin/env python3
"""Read-only GROUP-03.2 target availability capture with owned native loading and point input.

Use research/_env/live.sh to reserve B/C. No game calls or memory writes.
--mode control runs the identical map/input helper without attaching hooks.
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
    ap.add_argument('--preload', required=True)
    ap.add_argument('--marker-prefix', default='R264 ')
    ap.add_argument('--observer', default='rank264_observer.js')
    ap.add_argument('--observer-config', default='{}', help='extra JSON merged into the observer config')
    ap.add_argument('--mode', choices=('observe', 'control'), required=True)
    ap.add_argument('--remote', required=True)
    ap.add_argument('--x11-display', required=True)
    ap.add_argument('--env', choices=('B', 'C'), required=True)
    ap.add_argument('--expect-reject',action='store_true',help='end on observed original compiler failure; no script completion is claimed')
    ap.add_argument('--seconds', type=float, default=240)
    ap.add_argument('--continue-at', type=float, default=30)
    ap.add_argument('--input', action='append', nargs='+', metavar='ARG',
                    help='TICK X Y [alt] [shift] [key=move|none]: owned helper click at probe tick')
    ap.add_argument('--input-helper', type=Path)
    ap.add_argument('--input-seconds', type=float, nargs='*', default=None,
                    help='control mode: seconds after the --start-file appears, per input (control has no hooks)')
    ap.add_argument('--start-file', default=None, help='CustomMapData file the probe writes at tick 1 (control timing)')
    ap.add_argument('--output', type=Path, required=True, help='new JSONL path')
    args = ap.parse_args()
    if args.remote.split(':')[-1] in ('27046', '27048') or args.x11_display in (':94', ':97'):
        ap.error('refusing the owner environments')
    if {'B': ':98', 'C': ':99'}[args.env] != args.x11_display:
        ap.error('display/environment mismatch')
    if args.expect_reject and args.mode!='observe':ap.error('rejection completion needs the compiler observer')
    if args.output.exists():
        ap.error('output must be new')
    plan = []
    for item in args.input or []:
        extra = item[3:]
        key = 'move'
        for e in extra:
            if e.startswith('key='):
                key = e[4:]
            elif e not in ('alt', 'shift'):
                ap.error('--input TICK X Y [alt] [shift] [key=...]')
        plan.append(dict(tick=int(item[0]), pixel=[int(item[1]), int(item[2])], alt='alt' in extra, shift='shift' in extra, key=key))
    if plan and (not args.input_helper or not args.input_helper.is_file()):
        ap.error('--input requires the built owned helper')
    data = args.data.resolve()
    if 'w3-research2' not in str(data) and 'w3-research3' not in str(data):
        ap.error('research environment B/C install only')
    binary = (data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        ap.error('unsupported game.dll')
    pe = int.from_bytes(binary[0x3c:0x40], 'little')
    config = dict(timestamp=int.from_bytes(binary[pe + 8:pe + 12], 'little'), imageSize=int.from_bytes(binary[pe + 80:pe + 84], 'little'),
                  prefix=args.marker_prefix)
    config.update(json.loads(args.observer_config))
    map_path = data / args.map.replace('\\', '/')
    sources = [Path(__file__), HERE / args.observer, HERE / 'rank264_make_map.py', HERE / 'rank264_probe.j', HERE / 'queue263_ui_input.c', HERE / 'order0110_ui_input.c']
    if args.input_helper:
        sources += [args.input_helper, Path(str(args.input_helper) + '.so')]
    provenance = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    generated = data / 'CustomMapData' / args.preload
    started = data / 'CustomMapData' / args.start_file if args.start_file else None
    if started is not None and started.exists():
        Path(str(args.output.with_suffix('')) + '-previous-start.txt').write_bytes(started.read_bytes())
        started.unlink()
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
    env = {**os.environ, 'DISPLAY': args.x11_display, 'WINEPREFIX': PREFIXES[args.env], 'WINEDEBUG': '-all'}
    tick_re = re.compile(re.escape(args.marker_prefix) + r'tick=(\d+) ')
    with args.output.open('w', buffering=1) as out:
        def record(row):
            out.write(json.dumps(row) + '\n')

        def message(msg, _):
            payload = msg['payload'] if msg['type'] == 'send' else msg
            record(payload)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))
            if args.expect_reject and payload.get('event')=='compile-error':
                state['complete']=True
            if payload.get('event') == 'marker':
                v = payload.get('value', '')
                m = tick_re.match(v)
                if m:
                    state['tick'] = int(m.group(1))
                    if state['start'] is None and ' label=start' in v:
                        state['start'] = time.monotonic()
                if ' label=complete' in v:
                    state['complete'] = True
        try:
            pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record(dict(event='metadata', task='GROUP-03.2', mode=args.mode, sha256=HASH, pid=pid, owned=True,
                        source_sha256=provenance, map=args.map, seconds=args.seconds, continue_at=args.continue_at,
                        display=args.x11_display, remote=args.remote, env=args.env, input=plan, input_seconds=args.input_seconds,
                        frida=frida.__version__, **config))
            script = None
            if args.mode == 'observe':
                session = device.attach(pid)
                script = session.create_script('const config = ' + json.dumps(config) + ';\n' + (HERE / args.observer).read_text())
                script.on('message', message)
                script.load()
            device.resume(pid)
            start = time.monotonic()
            keys = 0
            next_key = start + args.continue_at
            done_at = None
            sent = 0
            control_start = None
            while time.monotonic() - start < args.seconds and not errors:
                now = time.monotonic()
                if args.mode == 'control' and control_start is None and ((started is not None and started.exists()) or generated.exists()):
                    control_start = now
                    record(dict(event='control-start-file', elapsed=now - start))
                ticking = state['tick'] >= 1 if args.mode == 'observe' else control_start is not None
                if state['start'] is not None and state['tick'] < 1:
                    next_key = min(next_key, state['start'] + 3)
                if not ticking and keys < 10 and now >= next_key:
                    res = subprocess.run([str(args.input_helper.resolve()), str(pid), 'continue'],
                                         timeout=5, env=env, capture_output=True, text=True)
                    record(dict(event='owned-loading-input', pid=pid, rc=res.returncode, stdout=res.stdout, stderr=res.stderr))
                    record(dict(event='loading-key', elapsed=now - start, tick=state['tick']))
                    keys += 1
                    next_key = now + 8
                due = False
                if sent < len(plan):
                    if args.mode == 'observe':
                        due = state['tick'] >= plan[sent]['tick']
                    elif control_start is not None and args.input_seconds is not None:
                        due = now - control_start >= args.input_seconds[sent]
                if due:
                    p = plan[sent]
                    cmd = [str(args.input_helper.resolve()), str(pid), *map(str, p['pixel'])] + (['shift'] if p['shift'] else []) + \
                          (['alt'] if p['alt'] else []) + ([p['key']] if p['key'] != 'none' else [])
                    res = subprocess.run(cmd, env=env, timeout=15, capture_output=True, text=True)
                    record(dict(event='player-input', at_tick=state['tick'], plan=p, cmd=cmd[1:], rc=res.returncode, stdout=res.stdout,
                                stderr=res.stderr[-2000:], elapsed=time.monotonic() - start))
                    sent += 1
                if done_at is None and (state['complete'] or (args.mode == 'control' and generated.exists() and
                                                              ' label=complete' in generated.read_text(errors='replace'))):
                    done_at = time.monotonic()
                if done_at is not None and time.monotonic() - done_at > 3:
                    break
                time.sleep(0.05)
            if script is not None:
                try:record(dict(event='trace-end', **script.exports_sync.finish()))
                except frida.InvalidOperationError as exc:
                    if not(args.expect_reject and state['complete']):raise
                    record(dict(event='observer-unavailable-after-diagnostic',reason=str(exc)))
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
                found = re.findall(r'call Preload\( "(' + re.escape(args.marker_prefix) + r'[^"\r\n]*)" \)', raw.decode('utf-8', 'replace'))
                record(dict(event='preload-file', sha256=hashlib.sha256(raw).hexdigest(), markers=len(found),
                            complete=any(' label=complete' in v for v in found)))
            else:
                record(dict(event='preload-file', missing=True,expected_reject=args.expect_reject,observed_reject=state['complete'] if args.expect_reject else False))


if __name__ == '__main__':
    main()
