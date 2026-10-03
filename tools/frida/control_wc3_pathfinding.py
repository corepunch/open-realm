#!/usr/bin/env python3
"""Run a copied pathfinding map without attaching/injecting; compare JASS output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def markers(text):
    return re.findall(r'call Preload\( "(PATH(?:TRACE|CROWD|TARGET|GATE|WIDGET|STOCK)[^"\r\n]*)" \)', text)


def validate(rows):
    samples = [int(re.search(r'tick=(\d+)', row)[1]) for row in rows
               if row.startswith('PATHTRACE ') and ' label=sample ' in row]
    if samples != list(range(1, 301)):
        raise ValueError('requires exactly the ordered 300 JASS samples')
    if not rows or not rows[-1].startswith('PATHTRACE tick=300 label=complete '):
        raise ValueError('missing terminal completion marker')


def main():
    import frida
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--map', required=True)
    parser.add_argument('--scenario', required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--reference-provenance', type=Path, help='defaults to reference stem + -provenance.json')
    parser.add_argument('--output', type=Path, required=True, help='new capture directory')
    parser.add_argument('--remote', default='127.0.0.1:27046')
    parser.add_argument('--seconds', type=float, default=130)
    parser.add_argument('--continue-at', type=float, default=80)
    parser.add_argument('--x11-display', default=':94')
    args = parser.parse_args()
    if not 0 < args.continue_at < args.seconds <= 3600:
        parser.error('require 0 < continue-at < seconds <= 3600')
    if not re.fullmatch(r'[a-z0-9_]+', args.scenario):
        parser.error('invalid scenario name')
    data = args.data.resolve()
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    if digest(data / 'game.dll') != HASH:
        parser.error('unsupported game.dll')
    map_path = (data / args.map.replace('\\', '/')).resolve()
    if not map_path.is_relative_to(data):
        parser.error('map must be inside data directory')
    reference = [json.loads(line) for line in args.reference.read_text().splitlines()]
    metadata = next(row for row in reference if row.get('event') == 'metadata')
    if metadata['sha256'] != HASH or metadata['map'] != args.map:
        parser.error('reference binary/map mismatch')
    reference_provenance = args.reference_provenance or args.reference.with_name(args.reference.stem + '-provenance.json')
    original = json.loads(reference_provenance.read_text())
    if original['binary']['sha256'] != HASH or original['map']['sha256'] != digest(map_path):
        parser.error('reference provenance binary/map hash mismatch')
    expected = [row['value'] for row in reference
                if row.get('event') in ('marker', 'crowd-marker', 'target-marker', 'widget-marker', 'stock-marker')]
    validate(expected)
    args.output.mkdir(parents=True, exist_ok=False)
    generated = data / 'CustomMapData' / ('pathtrace-' + args.scenario + '.txt')
    # Preserve old evidence and require a newly created file for this run.
    if generated.exists():
        (args.output / 'previous-preload.txt').write_bytes(generated.read_bytes())
        generated.unlink()
    provenance = dict(binary_sha256=HASH, map=args.map, map_sha256=digest(map_path),
                      reference=str(args.reference), reference_sha256=digest(args.reference),
                      reference_provenance_sha256=digest(reference_provenance),
                      controller_sha256=digest(Path(__file__)), frida=frida.__version__,
                      transport='Frida spawn/resume/kill only; no attach, script, or hooks',
                      seconds=args.seconds, continue_at=args.continue_at, display=args.x11_display)
    (args.output / 'controller.py').write_bytes(Path(__file__).read_bytes())
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(data).replace('/', '\\')
    pid = None
    try:
        pid = device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
        provenance['pid'] = pid
        device.resume(pid)
        start = time.monotonic()
        sent = False
        while time.monotonic() - start < args.seconds:
            if not sent and time.monotonic() - start >= args.continue_at:
                subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III',
                                'windowfocus', '--sync', 'key', 'space'], check=True, timeout=5,
                               env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL)
                sent = True
            time.sleep(0.1)
        raw = generated.read_bytes()
        (args.output / 'preload.txt').write_bytes(raw)
        actual = markers(raw.decode('utf-8'))
        validate(actual)
        differences = [dict(index=i, expected=a, actual=b)
                       for i, (a, b) in enumerate(zip(expected, actual)) if a != b]
        result = dict(expected_markers=len(expected), actual_markers=len(actual),
                      equal=actual == expected, differences=differences,
                      precision='exact JASS marker strings; R2S positions have three decimals',
                      preload_sha256=hashlib.sha256(raw).hexdigest())
        (args.output / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))
        if not result['equal']:
            raise RuntimeError('JASS marker mismatch; see comparison.json')
    except Exception as error:
        provenance['error'] = str(error)
        raise
    finally:
        if pid is not None:
            try:
                device.kill(pid)
            except frida.ProcessNotFoundError:
                provenance['already_exited'] = True
        (args.output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')


if __name__ == '__main__':
    main()
