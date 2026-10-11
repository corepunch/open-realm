#!/usr/bin/env python3
"""Capture an original long-journey probe run (wc3_journey_probe.j) with batched Frida rows.

The general trace_wc3_pathfinding.py sends one message per hooked call; long journeys
stall its message pipeline after roughly 95k messages. This observer hooks only the
exported rows, PATHMETA markers and optional velocity commits, batches them, and stops
at the probe's completion marker. Output rows use the general tracer's event names.

  DISPLAY=:94 WINEPREFIX=... python tools/frida/trace_wc3_journey.py --data "$WC3_DATA" \
      --map 'Maps\\Journey\\LostTempleCrossing.w3m' --x11-display :94 --commits --output run.jsonl
"""
import argparse, hashlib, json, os, struct, subprocess, threading, time
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def call_spec(text):
    parts = text.split(':')
    return {'rva': int(parts[0], 16), 'args': int(parts[1]) if len(parts) > 1 and parts[1] else 0,
            'out': 'out' in parts[2:], 'group': 'group' in parts[2:],
            'peek': [int(p[1:], 16) for p in parts[2:] if p.startswith('e')]}


def main():
    import frida
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--map', required=True)
    p.add_argument('--remote', default='127.0.0.1:27046')
    p.add_argument('--x11-display', required=True, help='owned isolated X display for loading-screen keys')
    p.add_argument('--continue-at', type=float, default=30, help='first loading key, seconds after spawn')
    p.add_argument('--continue-after-start', type=float, default=3, help='second key after the probe start marker')
    p.add_argument('--seconds', type=float, default=1800, help='hard deadline')
    p.add_argument('--commits', action='store_true', help='also record every velocity commit')
    p.add_argument('--call', action='append', default=[], metavar="RVA[:ARGS[:out|group|eOFF]...]",
                   help='also record a thiscall function: ECX, ARGS stack words and EAX (out: dword at arg0 after '
                        'return; group: physical group snapshot at entry; eOFF: dword at ECX+OFF at entry)')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    binary = (a.data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH: p.error('unsupported game.dll; requires 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = {'timestamp': struct.unpack_from('<I', binary, pe + 8)[0], 'imageSize': struct.unpack_from('<I', binary, pe + 80)[0],
              'commits': a.commits, 'calls': [call_spec(c) for c in a.call]}
    here = Path(__file__)
    sources = [here, here.with_name('wc3_journey.js'), here.with_name('wc3_journey_probe.j'), here.with_name('wc3_unitdata_probe.j'), here.with_name('wc3_alliance_probe.j'), here.with_name('wc3_grouporder_probe.j'),
               here.with_name('make_wc3_journey_map.py')]
    map_path = a.data / a.map.replace('\\', '/')
    provenance = {s.name: hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}
    provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    device = frida.get_device_manager().add_remote_device(a.remote)
    root = 'Z:' + str(a.data.resolve()).replace('/', '\\')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    state = {'start': None, 'complete': False, 'errors': []}
    lock = threading.Lock()
    with a.output.open('w') as out:
        def record(row):
            with lock: out.write(json.dumps(row) + '\n')

        def message(msg, _data):
            if msg['type'] == 'error':
                state['errors'].append(msg.get('description', str(msg))); record({'type': 'error', **msg}); return
            payload = msg['payload']
            if payload.get('event') != 'batch': record(payload); return
            for row in payload['rows']:
                kind = row[0]
                if kind == 'metadata-row': record({'event': kind, 'kind': row[1], 'parent': row[2], 'child': row[3], 'word': row[4]})
                elif kind == 'velocity-commit': record({'event': kind, 'mover': row[1], 'after': row[2]})
                elif kind == 'call': record({'event': kind, 'rva': row[1], 'ecx': row[2], 'args': row[3], 'ret': row[4], 'out': row[5], 'group': row[6], 'peek': row[7]})
                else:
                    record({'event': kind, 'value': row[1]})
                    if 'label=start_journey' in row[1] and state['start'] is None: state['start'] = time.monotonic()
                    if row[1] == 'PATHMETA complete': state['complete'] = True

        pid = session = None
        try:
            pid = device.spawn([root + r'\war3.exe', '-window', '-loadfile', a.map], cwd=root)
            record({'event': 'metadata', 'sha256': HASH, 'pid': pid, 'map': a.map, 'source_sha256': provenance,
                    'frida': frida.__version__, **config})
            session = device.attach(pid)
            script = session.create_script('const config = ' + json.dumps(config) + ';\n' + here.with_name('wc3_journey.js').read_text())
            script.on('message', message); script.load(); device.resume(pid)
            spawned = time.monotonic(); keys = 0
            env = {**os.environ, 'DISPLAY': a.x11_display}
            while time.monotonic() - spawned < a.seconds and not state['complete'] and not state['errors']:
                due = (keys == 0 and time.monotonic() - spawned >= a.continue_at) or \
                      (keys == 1 and state['start'] is not None and time.monotonic() - state['start'] >= a.continue_after_start)
                if due:
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III', 'windowfocus', '--sync', 'key', 'space'],
                                   check=True, timeout=5, env=env, stdout=subprocess.DEVNULL)
                    record({'event': 'loading-key', 'elapsed': time.monotonic() - spawned}); keys += 1
                time.sleep(0.1)
            record({'event': 'trace-end', 'complete': state['complete'], **script.exports_sync.finish()})
            if state['errors']: raise RuntimeError('; '.join(state['errors']))
            if not state['complete']: raise RuntimeError('probe did not complete before the deadline')
        finally:
            if pid is not None:
                try: device.kill(pid)
                except frida.ProcessNotFoundError: pass
            if session is not None:
                try: session.detach()
                except frida.InvalidOperationError: pass


if __name__ == '__main__':
    main()
