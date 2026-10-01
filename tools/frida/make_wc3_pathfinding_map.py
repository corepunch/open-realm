#!/usr/bin/env python3
"""Isolate a timed move/terrain edit in a copied Human02Interlude terrain archive."""
import argparse
from decimal import Decimal
import hashlib
import json
import math
import re
import struct
import subprocess
import tempfile
from pathlib import Path

SCENARIOS = {'open': 0, 'wall': 1, 'insert': 2, 'remove': 3, 'remove_reorder': 4,
             'gate': 5, 'gate_off': 6, 'gate_retarget': 7, 'gate_disable': 8, 'owner_change': 9, 'follow': 10, 'follow_shift': 11, 'follow_walk': 12, 'follow_invisible': 13, 'follow_fog': 14, 'follow_fog_reacquire': 15, 'blocked_goal': 16, 'crowd': 17, 'crowd_air': 18, 'widget_lifecycle': 19, 'turn': 20, 'stock_turn': 21, 'order_lifecycle': 22, 'numeric_inputs': 23, 'numeric_angles': 24, 'widget_escape': 25, 'widget_build_escape': 26, 'numeric_power': 27, 'numeric_literals': 28, 'numeric_integer_literals': 29, 'numeric_bytes': 30, 'profiles': 31, 'speed_inputs': 32, 'speed_drop': 33, 'item_speed': 34, 'item_speed_publish': 35, 'axis_position': 36}


def byte_unit_name(data):
    """Append one Footman name override; retain every original/custom object byte."""
    version, count = struct.unpack_from('<II', data)
    if version != 1:
        raise ValueError('byte fixture requires original version1 unit modifications')
    cursor = 8
    for _ in range(count):
        old, new, modifications = struct.unpack_from('<4s4sI', data, cursor)
        if old == b'hfoo':
            raise ValueError('Footman already has an original modification row')
        cursor += 12
        for _ in range(modifications):
            field, kind = struct.unpack_from('<4sI', data, cursor)
            cursor += 8
            if kind in (0, 1, 2):
                cursor += 4
            elif kind == 3:
                cursor = data.index(b'\0', cursor)+1
            else:
                raise ValueError('unsupported original unit modification type')
            cursor += 4
    # hfoo original override: unam string, terminated by null original object ID.
    row = b'hfoo'+b'\0'*4+struct.pack('<I', 1)+b'unam'+struct.pack('<I', 3)+bytes(range(128,256))+b'\0'*5
    return data[:4]+struct.pack('<I', count+1)+data[8:cursor]+row+data[cursor:]



def speed_unit_limits(data):
    """Append an authored hfoo clone; integer fields match retail UnitMetaData."""
    version = struct.unpack_from('<I', data)[0]
    if version != 1:
        raise ValueError('speed fixture requires version1 unit modifications')
    cursor = 4
    custom_count_at = None
    custom_ids = set()
    for table in range(2):
        if table == 1:
            custom_count_at = cursor
        count = struct.unpack_from('<I', data, cursor)[0]
        cursor += 4
        for _ in range(count):
            old, new, modifications = struct.unpack_from('<4s4sI', data, cursor)
            custom_ids.add(new)
            cursor += 12
            for _ in range(modifications):
                field, kind = struct.unpack_from('<4sI', data, cursor)
                cursor += 8
                if kind in (0, 1, 2):
                    cursor += 4
                elif kind == 3:
                    cursor = data.index(b'\0', cursor) + 1
                else:
                    raise ValueError('unsupported speed fixture modification type')
                cursor += 4
    if cursor != len(data) or b'h001' in custom_ids:
        raise ValueError('speed fixture trailing bytes or existing h001')
    row = b'hfoo' + b'h001' + struct.pack('<I', 3)
    for field, value in [(b'umvs', 237), (b'umis', 173), (b'umas', 389)]:
        row += field + struct.pack('<Ii', 0, value) + b'h001'
    count = struct.unpack_from('<I', data, custom_count_at)[0]
    return data[:custom_count_at] + struct.pack('<I', count + 1) + data[custom_count_at + 4:] + row


def numeric_calls(filename="wc3_numeric_inputs.json"):
    """Explicit public calls, each bracketed before argument evaluation can call the native."""
    cases = json.loads(Path(__file__).with_name(filename).read_text())['cases']
    lines = []
    seen = set()
    for case in cases:
        name, value, identity = case['native'], case['input'], case['id']
        if name not in ('S2R', 'I2R', 'R2I', 'Sin', 'Cos', 'Acos', 'SquareRoot', 'Asin', 'Atan', 'Tan', 'Atan2', 'Deg2Rad', 'Rad2Deg', 'Pow'):
            raise ValueError('unsupported numeric native: ' + name)
        if not re.fullmatch(r'[a-z0-9_]+', identity) or identity in seen:
            raise ValueError('duplicate or invalid numeric case ID')
        seen.add(identity)
        if case.get('producer') == 'byte_string':
            if name != 'S2R' or not isinstance(value, dict) or type(value.get('byte')) is not int or not 128 <= value['byte'] <= 255 or any(not isinstance(value.get(k), str) or any(ord(c) < 32 or ord(c) > 126 for c in value[k]) for k in ('prefix','suffix')):
                raise ValueError('byte fixture requires one high byte and ASCII prefix/suffix')
            offset = value['byte'] - 128
            argument = json.dumps(value['prefix']) + ' + SubString(byteSource, ' + str(offset) + ', ' + str(offset+1) + ') + ' + json.dumps(value['suffix'])
        elif name == 'S2R':
            if not isinstance(value, str) or any(ord(ch) < 32 or ord(ch) > 126 for ch in value):
                raise ValueError('numeric text fixture requires printable ASCII')
            argument = json.dumps(value)
        elif case.get('producer') == 'integer_literal':
            if name != 'I2R' or not isinstance(value, str) or not re.fullmatch(r'-?(?:[0-9]+|\$[0-9a-fA-F]+|0[xX][0-9a-fA-F]+)', value):
                raise ValueError('compiled integer fixture requires one integer token and I2R')
            argument = value
        elif name == 'I2R':
            if type(value) is not int or not -2147483648 <= value <= 2147483647:
                raise ValueError('I2R fixture requires signed integer')
            argument = str(value)
        elif case.get('producer') == 'literal':
            if name not in ('R2I', 'Sin', 'Cos') or not isinstance(value, str) or not re.fullmatch(r'-?(?:[0-9]+\.[0-9]*|\.[0-9]+)', value):
                raise ValueError('compiled literal fixture requires one decimal token and an observed unary native')
            argument = value
        else:
            values = value if name in ('Atan2', 'Pow') else [value]
            if (name in ('Atan2', 'Pow') and (not isinstance(values, list) or len(values) != 2)) or any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                raise ValueError('numeric real fixture requires finite arguments')
            # Feed decimal text through the observed public parser; long compiled
            # JASS literals have a separate producer contract.
            argument = ', '.join('S2R(' + json.dumps(format(Decimal(str(v)), 'f')) + ')' for v in values)
        lines.append('    call Preload("PATHNUM case=' + identity + ' native=' + name + '")')
        lines.append('    set ' + ('integerResult' if name == 'R2I' else 'realResult') + ' = ' + name + '(' + argument + ')')
        lines.append('    call Preload("PATHNUM done=' + identity + ' value=" + ' + ('I2S(integerResult)' if name == 'R2I' else 'R2S(realResult)') + ')')
    return '\n'.join(lines)


def instrument(script, probe, scenario, remove_tick=50, gate_y=-800.0, gate_exit_y=-240.0):
    if not 11 <= remove_tick < 300:
        raise ValueError('removal tick must follow the order and precede completion')
    if not all(math.isfinite(v) and -3072 <= v <= 5120 for v in (gate_y, gate_exit_y)):
        raise ValueError('gate Y coordinates must be finite and inside the source map')
    # Exact expected call sites prevent accidental edits to another campaign map.
    for old, new in [('call CreateAllUnits(  )', 'call PathProbeInit()'),
                     ('call InitCustomTriggers(  )', '// Path probe: scripted campaign orders disabled.'),
                     ('call RunInitializationTriggers(  )', '// Path probe: timer owns this experiment.')]:
        if script.count(old) != 1:
            raise ValueError('requires one Human02Interlude main call: ' + old)
        script = script.replace(old, new)
    if 'gg_rct_SorcAFight = Rect( -1856.0, -352.0, -1728.0, -224.0 )' not in script:
        raise ValueError('unexpected source map geometry')
    probe = probe.replace('@SCENARIO@', str(SCENARIOS[scenario])).replace('@NAME@', scenario)
    probe = probe.replace('@NUMERIC_CASES@', numeric_calls())
    probe = probe.replace('@ANGLE_CASES@', numeric_calls('wc3_angle_inputs.json'))
    probe = probe.replace('@POWER_CASES@', numeric_calls('wc3_power_inputs.json'))
    probe = probe.replace('@LITERAL_CASES@', numeric_calls('wc3_literal_inputs.json'))
    probe = probe.replace('@INTEGER_CASES@', numeric_calls('wc3_integer_inputs.json'))
    probe = probe.replace('@BYTE_CASES@', numeric_calls('wc3_byte_inputs.json'))
    # High bytes are string data, not unverified JASS source characters.
    probe = probe.replace('@BYTE_SOURCE@', 'GetUnitName(udg_PathProbeUnit)')
    probe = probe.replace('@REMOVE_TICK@', str(remove_tick))
    probe = probe.replace('@GATE_Y@', str(float(gate_y))).replace('@GATE_EXIT_Y@', str(float(gate_exit_y)))
    block = re.search(r'^globals\n(.*?)^endglobals\n', probe, re.M | re.S)
    if block is None or script.count('\nendglobals') != 1:
        raise ValueError('expected one globals block per script')
    script = script.replace('\nendglobals', '\n' + block.group(1) + 'endglobals', 1)
    return script.replace('\nendglobals', '\nendglobals\n' + probe[block.end():], 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool'))
    parser.add_argument('--scenario', choices=SCENARIOS, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--remove-tick', type=int, default=50)
    parser.add_argument('--gate-y', type=float, default=-800.0)
    parser.add_argument('--gate-exit-y', type=float, default=-240.0)
    args = parser.parse_args()
    if args.base.resolve() == args.output.resolve() or args.output.exists():
        parser.error('output must be a new file, distinct from the source map')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        parser.error('requires the original 512-byte wrapped campaign map')
    probe = Path(__file__).with_name('wc3_pathfinding_probe.j').read_text()
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    if members.count('war3map.j') != 1:
        parser.error('source must have exactly one war3map.j')
    if args.scenario in ('numeric_bytes', 'speed_inputs') and members.count('war3map.w3u') != 1:
        parser.error('object probe requires the original unit modification member')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wc3-path-map-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if member == 'war3map.j':
                source = data.decode('utf-8').replace('\r\n', '\n')
                data = instrument(source, probe, args.scenario, args.remove_tick, args.gate_y, args.gate_exit_y).encode('utf-8')
                args.output.with_suffix('.j').write_bytes(data)
            if member == 'war3map.w3u' and args.scenario == 'numeric_bytes':
                data = byte_unit_name(data)
                args.output.with_suffix('.w3u').write_bytes(data)
            if member == 'war3map.w3u' and args.scenario == 'speed_inputs':
                data = speed_unit_limits(data)
                args.output.with_suffix('.w3u').write_bytes(data)
            path = root / str(i)
            path.write_bytes(data)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = {'base_sha256': hashlib.sha256(original).hexdigest(), 'scenario': args.scenario,
              'remove_tick': args.remove_tick,
              'gate_y': args.gate_y, 'gate_exit_y': args.gate_exit_y,
              'map_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(), 'members': members,
              'changed_members': ['war3map.j', 'war3map.w3u'] if args.scenario in ('numeric_bytes', 'speed_inputs') else ['war3map.j'],
              'script_encoding': 'UTF-8',
              'byte_string_source': 'hfoo unam object field, GetUnitName, raw bytes 80..ff' if args.scenario == 'numeric_bytes' else None,
              'container': 'rebuilt MPQ with original HM3W header; signature not retained'}
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
