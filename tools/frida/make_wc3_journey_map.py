#!/usr/bin/env python3
"""Build a long-journey probe map from an original map and a journey spec.

The original terrain, pathing, doodads and destructables are retained. Only war3map.j
changes: preplaced unit creation and campaign/melee triggers are replaced by
wc3_journey_probe.j, filled from tools/frida/journeys/<name>.json. Original and engine
replays load the same output file, so both run the same script on the same world.

  python3 tools/frida/make_wc3_journey_map.py --data "$WC3_DATA" \
      --spec tools/frida/journeys/losttemple_crossing.json --output "$WC3_DATA/Maps/Journey/LostTempleCrossing.w3m"
"""
import argparse, hashlib, json, re, subprocess, tempfile
from pathlib import Path

PROBE = Path(__file__).with_name('wc3_journey_probe.j')
UNITDATA = Path(__file__).with_name('wc3_unitdata_probe.j')
ALLIANCES = Path(__file__).with_name('wc3_alliance_probe.j')
GROUPORDER = Path(__file__).with_name('wc3_grouporder_probe.j')
PATHING = Path(__file__).with_name('wc3_pathing_probe.j')
MAIN_CALLS = [('call CreateAllUnits(  )', 'call PathProbeInit()'),
              ('call InitCustomTriggers(  )', '// Journey probe: map triggers disabled.'),
              ('call RunInitializationTriggers(  )', '// Journey probe: timer owns this experiment.')]


def real(value):
    value = float(value)
    if value * 8 != int(value * 8): raise ValueError(f'coordinate {value} is not an exact eighth')
    return repr(value)


def fourcc(code):
    if not re.fullmatch(r'[A-Za-z0-9]{4}', code): raise ValueError(f'unit type {code!r}')
    return f"'{code}'"


def order_name(name):
    if not re.fullmatch(r'[a-z]+', name): raise ValueError(f'order {name!r}')
    return f'"{name}"'


def timeline(spec, count):
    by_tick = {}
    for step in spec['timeline']:
        tick = step['tick']
        if not 1 <= tick <= spec['ticks']: raise ValueError(f'timeline tick {tick} outside capture')
        if 'group' in step:
            first, last = step['group']
            if not 0 <= first <= last < count or last - first >= 12: raise ValueError('group must name 1..12 subjects')
            line = f" call JourneyGroupPoint({first},{last},{order_name(step['order'])},{real(step['to'][0])},{real(step['to'][1])})"
        elif 'immediate' in step:
            line = f" call JourneyImmediate({step['unit']},{order_name(step['immediate'])})"
        elif 'target' in step:
            line = f" call JourneyTarget({step['unit']},{order_name(step['order'])},{step['target']})"
        else:
            line = f" call JourneyPoint({step['unit']},{order_name(step['order'])},{real(step['to'][0])},{real(step['to'][1])})"
        for key in ('unit', 'target'):
            if key in step and not 0 <= step[key] < count: raise ValueError(f'{key} {step[key]} outside subjects')
        by_tick.setdefault(tick, []).append(line)
    lines = []
    for tick in sorted(by_tick):
        lines.append(f' if udg_PathProbeTick=={tick} then')
        lines.extend(' ' + line for line in by_tick[tick])
        lines.append(' endif')
    return '\n'.join(lines)


def probe_source(spec):
    if spec.get('probe') == 'alliances': return ALLIANCES.read_text()
    if spec.get('probe') == 'grouporder':
        lines = []
        for case in spec['cases']:
            lines.append(' set g=CreateGroup()')
            lines.extend(f' call GroupOrderAdd(g,{fourcc(t)},{i})' for i, t in enumerate(case))
            lines.append(f' call GroupOrderCase(g,{len(case)})')
        return GROUPORDER.read_text().replace('@CASES@', '\n'.join(lines))
    if spec.get('probe') == 'pathing':
        x0, y0, x1, y1 = spec['box']; step = spec.get('step', 32)
        cols, rows = int((x1 - x0) // step) + 1, int((y1 - y0) // step) + 1
        if cols > 256: raise ValueError('pathing rows hold at most 256 cells')
        return (PATHING.read_text().replace('@X0@', real(x0)).replace('@Y0@', real(y0)).replace('@STEP@', real(step))
                .replace('@COLS@', str(cols)).replace('@ROWS@', str(rows)))
    if spec.get('probe') == 'unitdata':
        return UNITDATA.read_text().replace('@TYPES@', '\n'.join(f" call UnitDataRecord({fourcc(t)})" for t in spec['types']))
    units = spec['units']
    if not 1 <= len(units) <= 64: raise ValueError('journeys contain 1..64 subjects')
    if spec['sample_every'] < 1 or spec['ticks'] < spec['sample_every']: raise ValueError('sample cadence')
    created = '\n'.join(f" call JourneyAdd({u.get('owner', 0)},{fourcc(u['type'])},{real(u['at'][0])},{real(u['at'][1])},{real(u.get('facing', 270))})" for u in units)
    return (PROBE.read_text().replace('@UNITS@', created).replace('@TIMELINE@', timeline(spec, len(units)))
            .replace('@SAMPLE_EVERY@', str(spec['sample_every'])).replace('@TICKS@', str(spec['ticks']))
            .replace('@CAMERA_X@', real(spec['camera'][0])).replace('@CAMERA_Y@', real(spec['camera'][1])))


def instrument(script, probe):
    for old, new in MAIN_CALLS:
        if script.count(old) != 1: raise ValueError('map main differs: ' + old)
        script = script.replace(old, new)
    block = re.search(r'^globals\n(.*?)^endglobals\n', probe, re.M | re.S)
    if block is None or script.count('\nendglobals') != 1: raise ValueError('expected one globals block')
    script = script.replace('\nendglobals', '\n' + block.group(1) + 'endglobals', 1)
    return script.replace('\nendglobals', '\nendglobals\n' + probe[block.end():], 1)


def base_bytes(tool, data, base):
    if isinstance(base, str): return (data / base).read_bytes()
    return subprocess.check_output([tool, '-mpq', str(data / base['mpq']), 'cat', base['member']])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data', type=Path, required=True, help='retail installation root')
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool'))
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text())
    tool = str(args.tool.resolve())
    original = base_bytes(tool, args.data, spec['base'])
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a': parser.error('requires a 512-byte wrapped map')
    probe = probe_source(spec)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wc3-journey-') as temp:
        root = Path(temp); source = root / 'base.w3m'; source.write_bytes(original)
        members = subprocess.check_output([tool, '-mpq', str(source), 'ls']).decode().splitlines()
        if members.count('war3map.j') != 1: parser.error('source must have exactly one war3map.j')
        command = [tool, '-mpq', str(root / 'payload.mpq'), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(source), 'cat', member])
            if member == 'war3map.j':
                data = instrument(data.decode('utf-8').replace('\r\n', '\n'), probe).encode('utf-8')
                args.output.with_suffix('.j').write_bytes(data)
            path = root / str(i); path.write_bytes(data); command.extend([str(path), member])
        subprocess.run(command, check=True, capture_output=True)
        args.output.write_bytes(original[:512] + (root / 'payload.mpq').read_bytes())
    result = {'spec': spec['name'], 'spec_sha256': hashlib.sha256(args.spec.read_bytes()).hexdigest(),
              'probe_sha256': hashlib.sha256({'unitdata': UNITDATA, 'alliances': ALLIANCES, 'grouporder': GROUPORDER, 'pathing': PATHING}.get(spec.get('probe'), PROBE).read_bytes()).hexdigest(),
              'base_sha256': hashlib.sha256(original).hexdigest(),
              'map_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(), 'changed_members': ['war3map.j']}
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
