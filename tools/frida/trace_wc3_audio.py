#!/usr/bin/env python3
"""Bounded Frida trace of the owned, hash-checked WC3 retail process under Wine."""
import argparse, hashlib, json, os, subprocess, threading, time
from pathlib import Path
import frida

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--data', type=Path, required=True)
parser.add_argument('--remote', default='127.0.0.1:27043')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--pid', type=int)
parser.add_argument('--seconds', type=int, default=90)
parser.add_argument('--map', default=r'Maps\Campaign\Human01.w3m')
parser.add_argument('--drive-input', action='store_true')
parser.add_argument('--x11-display', help='use xdotool in an isolated X display instead of Windows posted messages')
parser.add_argument('--probe-aliases', action='store_true', help='replace the first two What labels with a shared-file pair')
parser.add_argument('--probe-preemption', action='store_true', help='enable duplicate-user preemption on one request during an active response')
parser.add_argument('--burst-input', action='store_true', help='send repeated Hero selection every 350ms after cinematic dismissal')
args = parser.parse_args()
expected = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
actual = hashlib.sha256((args.data / 'game.dll').read_bytes()).hexdigest()
if actual != expected: parser.error('game.dll is not the mapped 1.27.1.7085 binary')
if args.seconds <= 0: parser.error('--seconds must be positive')
device = frida.get_device_manager().add_remote_device(args.remote)
spawned = args.pid is None
windows_path = 'Z:' + str(args.data.resolve()).replace('/', '\\')
pid = args.pid or device.spawn([windows_path + r'\war3.exe', '-window', '-classic', '-loadfile', args.map], cwd=windows_path)
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open('w') as output:
    lock = threading.Lock()
    def record(data):
        with lock:
            output.write(json.dumps(data) + '\n'); output.flush()
    def message(msg, data):
        record(msg['payload'] if msg['type'] == 'send' else msg)
    record({'event': 'trace-metadata', 'binary_sha256': actual, 'frida': frida.__version__, 'pid': pid, 'map': args.map, 'spawned': spawned, 'probe_aliases': args.probe_aliases, 'probe_preemption': args.probe_preemption, 'x11_display': args.x11_display})
    def key(script, window, value):
        if args.x11_display:
            name = {27: 'Escape', 32: 'space', 112: 'F1'}[value]
            subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III',
                            'windowfocus', '--sync', 'key', '--clearmodifiers', name],
                           env={**os.environ, 'DISPLAY': args.x11_display}, check=True, timeout=5,
                           stdout=subprocess.DEVNULL)
            record({'event': 'x11-input', 'key': name, 'elapsed': time.monotonic() - start})
        else: script.exports_sync.key(window['handle'], value)
    session = None
    try:
        session = device.attach(pid)
        source = 'const expectedModulePath = ' + json.dumps(windows_path + r'\game.dll') + ';\n'
        script = session.create_script(source + Path(__file__).with_name('wc3_audio.js').read_text())
        script.on('message', message); script.load()
        if args.probe_aliases: script.exports_sync.enableprobe()
        if args.probe_preemption: script.exports_sync.enablepreemption()
        if spawned: device.resume(pid)
        start = time.monotonic(); step = 0; burst_step = 0
        while time.monotonic() - start < args.seconds:
            time.sleep(0.1)
            elapsed = time.monotonic() - start
            if args.drive_input and elapsed >= 10 + step * 5 and not (args.burst_input and elapsed >= 55):
                status = script.exports_sync.status(); record({'event': 'status', **status})
                if status['windows']:
                    window = max(status['windows'], key=lambda w: w['width']*w['height'])
                    if step < 2: key(script, window, 27)
                    elif step == 2: key(script, window, 112) # F1 selects Hero
                    elif step == 3: key(script, window, 32) # dismiss mission text
                    elif step in (4, 5, 6): key(script, window, 27) # loading may outlast the early Esc keys
                    else: key(script, window, 112)
                step += 1
            if args.burst_input and elapsed >= 55 + burst_step * 0.35:
                status = script.exports_sync.status()
                if status['windows']:
                    window = max(status['windows'], key=lambda w: w['width']*w['height'])
                    key(script, window, 112)
                burst_step += 1
    except Exception as error:
        record({'type': 'error', 'description': str(error)})
        raise
    finally:
        try:
            if session: session.detach()
        finally:
            if spawned:
                try: device.kill(pid)
                except frida.ProcessNotFoundError:
                    record({'event': 'process-already-exited', 'pid': pid})
        record({'event': 'trace-closed', 'pid': pid})
