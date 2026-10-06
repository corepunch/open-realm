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

SCENARIOS = {'formation_blocked':110, 'formation_policy':109, 'formation_ranks':108, 'mover_retirement':107, 'scheduler_mutation':106, 'scheduler_nonunit':104, 'scheduler_contention':102, 'gate_overlap':94, 'gate_capacity':93, 'fine_results':91, 'speed_modifiers':88, 'movement_modes':87, 'movement_bypasses':86, 'region_callbacks':85, 'movement_lifecycle':84, 'terrain_cache':83, 'chained_expression':82, 'target_overlap': 81, 'adaptive_passage': 80, 'widget_overlap_orders': 73, 'blocker_lifecycle': 72, 'open': 0, 'wall': 1, 'insert': 2, 'remove': 3, 'remove_reorder': 4,
             'gate': 5, 'gate_off': 6, 'gate_retarget': 7, 'gate_disable': 8, 'owner_change': 9, 'follow': 10, 'follow_shift': 11, 'follow_walk': 12, 'follow_invisible': 13, 'follow_fog': 14, 'follow_fog_reacquire': 15, 'blocked_goal': 16, 'crowd': 17, 'crowd_air': 18, 'widget_lifecycle': 19, 'turn': 20, 'stock_turn': 21, 'order_lifecycle': 22, 'numeric_inputs': 23, 'numeric_angles': 24, 'widget_escape': 25, 'widget_build_escape': 26, 'numeric_power': 27, 'numeric_literals': 28, 'numeric_integer_literals': 29, 'numeric_bytes': 30, 'profiles': 31, 'speed_inputs': 32, 'speed_drop': 33, 'item_speed': 34, 'item_speed_publish': 35, 'axis_position': 36, 'clock_oblique': 37, 'forced_position': 38, 'blocked_position': 39, 'random_owner': 40, 'pathing_toggle': 41, 'pathing_position': 42, 'stop_recovery': 43, 'spawn_admission': 44, 'public_oblique': 45, 'group_orders': 46, 'group_pair': 47, 'group_twelve': 48, 'selected_point_pair': 49, 'selected_point_queued_pair': 50, 'selected_point_mixed_pair': 51, 'selected_point_independent_pair': 52, 'follow_velocity': 53, 'follow_target_remove_reuse': 54, 'follow_target_kill_reuse': 55, 'follow_target_xy': 56, 'follow_target_position': 57, 'follow_target_travel_xy': 58, 'follow_target_travel_position': 59, 'follow_target_grow': 60, 'follow_target_shrink': 61, 'follow_target_resize_gate': 62, 'moving_radius': 63, 'moving_radius_matrix': 64, 'group_radius_grow': 65, 'group_radius_shrink': 66, 'group_radius_remove': 67, 'outside_west': 68, 'point_bound_matrix': 69, 'captain_home': 71}


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



def random_calls():
    """Drive real public native seed/draw contracts, including reversed and wrapped bounds."""
    fixture=json.loads(Path('tools/ghidra/fixtures/retail-pathfinding-random-1.27.json').read_text())
    lines=[]
    for sequence in fixture['sequences']:
        seed=sequence['seed']; signed=seed if seed<0x80000000 else seed-0x100000000
        lines += [f'    call Preload("PATHRANDOM case=seed_{seed} native=SetRandomSeed")',f'    call SetRandomSeed({signed})']
        for i,op in enumerate(sequence['operations'][:48]):
            native='GetRandomReal' if op['kind']==2 else 'GetRandomInt'
            if op['kind']==1:
                args=','.join(str(v if v<0x80000000 else v-0x100000000) for v in op['input'])
            elif op['kind']==2:
                floats=struct.unpack('<ff',struct.pack('<II',*op['input']))
                args=','.join(f'(I2R({v.as_integer_ratio()[0]})/I2R({v.as_integer_ratio()[1]}))' for v in floats)
            else: args='0,1'
            variable='realResult' if native=='GetRandomReal' else 'integerResult'
            lines += [f'    call Preload("PATHRANDOM case=seed_{seed}_op_{i} native={native}")',f'    set {variable}={native}({args})']
    lines.append('    call Preload("PATHRANDOM done=all")')
    return '\n'.join(lines)


def instrument(script, probe, scenario, remove_tick=50, gate_y=-800.0, gate_exit_y=-240.0, captain_source_y=-976.0, captain_peer=False, captain_peer_type="hfoo", captain_blocked_home=False, captain_pool=None, captain_third=False, captain_thirteen=False):
    if not 11 <= remove_tick < 300:
        raise ValueError('removal tick must follow the order and precede completion')
    if not all(math.isfinite(v) and -3072 <= v <= 5120 for v in (gate_y, gate_exit_y)):
        raise ValueError('gate Y coordinates must be finite and inside the source map')
    if not math.isfinite(captain_source_y) or not -3072 <= captain_source_y <= 5120:
        raise ValueError('captain source Y must be finite and inside the source map')
    if captain_peer_type not in ('hfoo','hkni') or (captain_peer_type!='hfoo' and not captain_peer):
        raise ValueError('captain peer profile requires an explicit supported peer')
    if captain_pool not in (None,'reuse','transfer','same_owner','partial') or (captain_pool and (not captain_peer or captain_peer_type!='hfoo' or captain_blocked_home)):
        raise ValueError('captain pool requires an unblocked Footman peer')
    if captain_thirteen and (captain_peer or captain_third or captain_pool or captain_peer_type!='hfoo' or captain_blocked_home):
        raise ValueError('captain thirteen requires an unblocked Footman roster without other variants')
    if captain_third and (not captain_peer or captain_peer_type!='hfoo' or captain_blocked_home or captain_pool):
        raise ValueError('captain third requires an unblocked Footman pair without pool mutation')
    if (captain_peer or captain_blocked_home or captain_pool or captain_third or captain_thirteen) and scenario != 'captain_home':
        raise ValueError('captain peer requires captain_home')
    if scenario == 'captain_home':
        if captain_blocked_home:
            guard='    if PATH_PROBE_SCENARIO == 16 then'
            if probe.count(guard)!=1:raise ValueError('Blocked home terrain producer differs')
            probe=probe.replace(guard,'    if PATH_PROBE_SCENARIO == 16 or PATH_PROBE_SCENARIO == 71 then')
        if captain_thirteen:
            create='    set udg_PathProbeUnit = CreateUnit(Player(0), crowdType, -1936.0, -976.0, 90.0)'
            if probe.count(create)!=1:raise ValueError('Captain recruit creation differs')
            births='\n    if PATH_PROBE_SCENARIO==71 then\n        set udg_PathProbeCrowd[0]=udg_PathProbeUnit\n'
            for i in range(1,13):
                x=-1936+(i%4)*80;y=captain_source_y-(i//4)*80
                births+=f"        set udg_PathProbeCrowd[{i}]=CreateUnit(Player(0),'hfoo',{x:.1f},{y:.1f},90.0)\n"
                births+=f'        call SetUnitMoveSpeed(udg_PathProbeCrowd[{i}],100.0)\n'
            probe=probe.replace(create,create+births+'    endif')
        if captain_peer:
            peer_type="'hkni'" if captain_peer_type=='hkni' else 'crowdType'
            create='    set udg_PathProbeUnit = CreateUnit(Player(0), crowdType, -1936.0, -976.0, 90.0)'
            if probe.count(create)!=1:raise ValueError('Captain recruit creation differs')
            probe=probe.replace(create,create+'\n    if PATH_PROBE_SCENARIO==71 then\n'
                '        set udg_PathProbeCrowd[0]=udg_PathProbeUnit\n'
                f'        set udg_PathProbeCrowd[1]=CreateUnit(Player(0),{peer_type},-1856.0,{captain_source_y:.1f},90.0)\n'
                '        call SetUnitMoveSpeed(udg_PathProbeCrowd[1],100.0)\n    endif')
        if captain_third:
            second='        call SetUnitMoveSpeed(udg_PathProbeCrowd[1],100.0)\n    endif'
            if probe.count(second)!=1:raise ValueError('Captain pair creation differs')
            probe=probe.replace(second,'        call SetUnitMoveSpeed(udg_PathProbeCrowd[1],100.0)\n'
                f"        set udg_PathProbeCrowd[2]=CreateUnit(Player(0),'hfoo',-1776.0,{captain_source_y:.1f},90.0)\n"
                '        call SetUnitMoveSpeed(udg_PathProbeCrowd[2],100.0)\n    endif')
        probe=probe.replace('CreateUnit(Player(0), crowdType, -1936.0, -976.0, 90.0)',f'CreateUnit(Player(0), crowdType, -1936.0, {captain_source_y:.1f}, 90.0)')
        if captain_pool:
            start='    if PATH_PROBE_SCENARIO == 71 and udg_PathProbeTick == 10 then'
            before='        call PathProbeRecord("before_captain_ai")'
            after='        call PathProbeRecord("after_captain_ai")'
            if any(probe.count(v)!=1 for v in (start,before,after)):
                raise ValueError('captain pool lifecycle producer differs')
            if captain_pool=='reuse':
                probe=probe.replace(start,'    if PATH_PROBE_SCENARIO == 71 and udg_PathProbeTick == 5 then\n'
                    '        call Preload("PATHCAPTAIN pool delayed remove")\n'
                    '        call RemoveUnit(udg_PathProbeUnit)\n'
                    '        set udg_PathProbeUnit=null\n        set udg_PathProbeCrowd[0]=null\n    endif\n'
                    '    if PATH_PROBE_SCENARIO == 71 and udg_PathProbeTick == 20 then')
                label='delayed create'
                action="        set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',-1936.0,-976.0,90.0)\n"+                    '        set udg_PathProbeCrowd[0]=udg_PathProbeUnit\n        call SetUnitMoveSpeed(udg_PathProbeUnit,100.0)'
            else:
                label='transfer' if captain_pool in ('transfer','partial') else 'same-owner'
                action=('        call SetUnitOwner(udg_PathProbeUnit,Player(1),false)\n' if captain_pool in ('transfer','partial') else '')+                    '        call SetUnitOwner(udg_PathProbeUnit,Player(0),false)'
            marker=' primary="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit))+" peer="+I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[1])))'
            probe=probe.replace(before,before+'\n        call Preload("PATHCAPTAIN pool before '+label+marker+'\n'+action)
            probe=probe.replace(after,'        call Preload("PATHCAPTAIN pool after recruit'+marker+'\n'+after)
        original_controller='call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )'
        if script.count(original_controller)!=1:raise ValueError('Captain player config differs')
        script=script.replace(original_controller,'call SetPlayerController( Player(0), MAP_CONTROL_COMPUTER )')
    # Exact expected call sites prevent accidental edits to another campaign map.
    for old, new in [('call CreateAllUnits(  )', 'call PathProbeInit()'),
                     ('call InitCustomTriggers(  )', '// Path probe: scripted campaign orders disabled.'),
                     ('call RunInitializationTriggers(  )', '// Path probe: timer owns this experiment.')]:
        if script.count(old) != 1:
            raise ValueError('requires one Human02Interlude main call: ' + old)
        script = script.replace(old, new)
    if 'gg_rct_SorcAFight = Rect( -1856.0, -352.0, -1728.0, -224.0 )' not in script:
        raise ValueError('unexpected source map geometry')
    if scenario in ('adaptive_passage','target_overlap','chained_expression','terrain_cache','movement_lifecycle','region_callbacks','movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked'):
        script=re.sub(r'call SetCameraBounds\( [^\n]+', 'call SetCameraBounds( 128, 128, 1920, 1920, 128, 1920, 1920, 128 )',script)
        script=re.sub(r'call DefineStartLocation\( ([0-3]), [^\n]+',r'call DefineStartLocation( \1, 272, 304 )',script)
        script=script.replace('call CreateRegions(  )','// Flat passage has no campaign regions.')
        script=script.replace('call CreateCameras(  )','// Flat passage has no campaign cameras.')
    probe = probe.replace('@SCENARIO@', str(SCENARIOS[scenario])).replace('@NAME@', scenario)
    probe = probe.replace('@RANDOM_CASES@', random_calls())
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


def resize_units(data, matrix=False, clones=None):
    version=struct.unpack_from('<I',data)[0]
    if version != 1: raise ValueError('resize requires version1 unit modifications')
    cursor=4
    custom_ids=set()
    for table in range(2):
        count_at=cursor;count=struct.unpack_from('<I',data,cursor)[0];cursor+=4
        for _ in range(count):
            old,new,n=struct.unpack_from('<4s4sI',data,cursor);cursor+=12
            custom_ids.add(new)
            for _ in range(n):
                field,kind=struct.unpack_from('<4sI',data,cursor);cursor+=8
                if kind in (0,1,2):cursor+=4
                elif kind==3:cursor=data.index(b'\0',cursor)+1
                else:raise ValueError('unsupported unit modification type')
                cursor+=4
    if cursor != len(data):raise ValueError('trailing unit modification data')
    if clones is None:
        clones=[(('hc%02d'%i).encode(),radius) for i,radius in enumerate([15.9921875,16,16.0078125,31.9921875,32,32.0078125,47.9921875,48,48.0078125],1)] if matrix else [(b'hCLG',63.0),(b'hCLS',7.0)]
    if any(len(code)!=4 or code in custom_ids for code,radius in clones) or len({code for code,radius in clones})!=len(clones):
        raise ValueError('resize clone already exists or has invalid identity')
    out=data[:count_at]+struct.pack('<I',count+len(clones))+data[count_at+4:]
    for code,radius in clones:
        out+=b'hfoo'+code+struct.pack('<I',1)+b'ucol'+struct.pack('<I',2)+struct.pack('<f',radius)+code
    return out


def passage_map_member(member, data, blocked=None, modes=False):
    """Load the reduced witness through ordinary terrain/WPM/object producers."""
    fixture=Path(__file__).resolve().parents[1]/'ghidra/fixtures/pathing-adaptive-size2-passage.json'
    scene=json.loads(fixture.read_text())
    if scene['dimensions']!=[32,32]:raise ValueError('reduced passage dimensions differ')
    if member=='war3map.w3e':
        if data[:4]!=b'W3E!':raise ValueError('invalid terrain member')
        ground_count=struct.unpack_from('<I',data,13)[0]
        cliff_at=17+ground_count*4
        cliff_count=struct.unpack_from('<I',data,cliff_at)[0]
        size_at=cliff_at+4+cliff_count*4
        # 16x16 flat terrain tiles contain the 64x64 fine pathing grid.
        return data[:size_at]+struct.pack('<IIff',17,17,0,0)+struct.pack('<HHBBB',8192,8192,0,0,2)*289
    if member=='war3map.wpm':
        cells=bytearray(64*64)
        for x,y in scene['blocked'] if blocked is None else blocked:
            for dy in range(2):
                for dx in range(2):cells[(y*2+dy)*64+x*2+dx]=0xc6
        return b'MP3W'+struct.pack('<III',0,64,64)+cells
    if member in ('war3map.doo','war3mapUnits.doo'):
        if data[:4]!=b'W3do':raise ValueError('invalid placement member')
        return data[:12]+struct.pack('<I',0)+(struct.pack('<II',0,0) if member=='war3map.doo' else b'')
    if member=='war3map.w3u':
        if not modes:return resize_units(data,clones=[(b'hV80',40.)])
        out=resize_units(data,clones=[(b'hV80',40.),(b'hAIR',40.)])
        row=b'hfoo'+b'hAIR'+struct.pack('<I',1)
        if out.count(row)!=1:raise ValueError('mode clone row differs')
        out=out.replace(row,b'hfoo'+b'hAIR'+struct.pack('<I',3))
        return out+b'umvt'+struct.pack('<I',3)+b'fly\0hAIR'+b'umvh'+struct.pack('<If',2,60.)+b'hAIR'
    return data

def resize_ability(scenario):
    if scenario in ('formation_ranks','formation_policy','formation_blocked'):
        return struct.pack('<III',2,0,1)+b'Sca1AF03'+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+b'hF03\0AF03'+b'areq'+struct.pack('<III',3,0,0)+b'\0AF03'
    if scenario=='movement_modes':
        out=struct.pack('<III',2,0,2)
        for code,target in [(b'ACFl',b'hAIR'),(b'ACGr',b'hV80')]:
            out+=b'Sca1'+code+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code+b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
        return out
    if scenario in ('group_radius_grow','group_radius_shrink','group_radius_remove'):
        out=struct.pack('<III',2,0,2)
        for code,target in [(b'ACGb',b'hCLG'),(b'ACSh',b'hCLS')]:
            out+=b'Sca1'+code+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code+b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
        return out
    if scenario=='moving_radius_matrix':
        out=struct.pack('<III',2,0,9)
        for i in range(1,10):
            code=('Ag%02d'%i).encode();target=('hc%02d'%i).encode()
            out+=b'Sca1'+code+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code+b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
        return out
    code,target=(b'ACSh',b'hCLS') if scenario=='follow_target_shrink' else (b'ACGb',b'hCLG')
    out=struct.pack('<III',2,0,1)+b'Sca1'+code+struct.pack('<I',1 if scenario=='follow_target_resize_gate' else 2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code
    if scenario!='follow_target_resize_gate':out+=b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool'))
    parser.add_argument('--scenario', choices=SCENARIOS, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--remove-tick', type=int, default=50)
    parser.add_argument('--gate-y', type=float, default=-800.0)
    parser.add_argument('--gate-exit-y', type=float, default=-240.0)
    parser.add_argument('--captain-source-y',type=float,default=-976.0,help='captain_home recruit start Y; preserves captain creation phase')
    parser.add_argument('--captain-peer',action='store_true',help='add the second ground recruit and default to the two-recruit AI probe')
    parser.add_argument('--captain-peer-type',choices=('hfoo','hkni'),default='hfoo',help='second recruit profile; requires --captain-peer')
    parser.add_argument('--captain-blocked-home',action='store_true',help='block the five-by-five authored home terrain before admission')
    parser.add_argument('--captain-thirteen',action='store_true',help='add thirteen Footmen in four-column birth order for captain 12+1 admission')
    parser.add_argument('--captain-third',action='store_true',help='add a third Footman to an unblocked captain pair')
    parser.add_argument('--captain-pool',choices=('reuse','transfer','same_owner','partial'),help='single-recruit owned-pool lifecycle; requires --captain-peer')
    parser.add_argument('--captain-ai', type=Path, help='explicit captain_home AI script; default is the stationary home probe')
    args = parser.parse_args()
    if (args.captain_ai or args.captain_pool or args.captain_peer or args.captain_third or args.captain_thirteen or args.captain_blocked_home or args.captain_peer_type!='hfoo') and args.scenario != 'captain_home':
        parser.error('--captain-ai/--captain-peer require captain_home')
    if args.captain_peer_type!='hfoo' and not args.captain_peer:parser.error('--captain-peer-type requires --captain-peer')
    if args.captain_pool and (not args.captain_peer or args.captain_peer_type!='hfoo' or args.captain_blocked_home):
        parser.error('--captain-pool requires an unblocked Footman peer')
    if args.captain_thirteen and (args.captain_peer or args.captain_third or args.captain_pool or args.captain_peer_type!='hfoo' or args.captain_blocked_home):
        parser.error('--captain-thirteen requires an unblocked Footman roster without other variants')
    if args.captain_third and (not args.captain_peer or args.captain_peer_type!='hfoo' or args.captain_blocked_home or args.captain_pool):
        parser.error('--captain-third requires an unblocked Footman pair without pool mutation')
    captain_ai = args.captain_ai or Path(__file__).with_name('wc3_captain_thirteen_probe.ai' if args.captain_thirteen else 'wc3_captain_three_probe.ai' if args.captain_third else 'wc3_captain_pool_partial_probe.ai' if args.captain_pool=='partial' else 'wc3_captain_pool_probe.ai' if args.captain_pool else 'wc3_captain_mixed_probe.ai' if args.captain_peer_type=='hkni' else 'wc3_captain_pair_probe.ai' if args.captain_peer else 'wc3_captain_probe.ai')
    if args.scenario == 'captain_home' and not captain_ai.is_file():
        parser.error('captain AI source is missing')
    if args.base.resolve() == args.output.resolve() or args.output.exists():
        parser.error('output must be a new file, distinct from the source map')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        parser.error('requires the original 512-byte wrapped campaign map')
    probe = Path(__file__).with_name('wc3_formation_blocked_probe.j' if args.scenario == 'formation_blocked' else 'wc3_formation_policy_probe.j' if args.scenario == 'formation_policy' else 'wc3_formation_rank_probe.j' if args.scenario == 'formation_ranks' else 'wc3_mover_retirement_probe.j' if args.scenario == 'mover_retirement' else 'wc3_scheduler_mutation_probe.j' if args.scenario == 'scheduler_mutation' else 'wc3_scheduler_nonunit_probe.j' if args.scenario == 'scheduler_nonunit' else 'wc3_scheduler_probe.j' if args.scenario == 'scheduler_contention' else 'wc3_waygate_overlap_probe.j' if args.scenario == 'gate_overlap' else 'wc3_waygate_capacity_probe.j' if args.scenario == 'gate_capacity' else 'wc3_fine_results_probe.j' if args.scenario == 'fine_results' else 'wc3_speed_modifiers_probe.j' if args.scenario == 'speed_modifiers' else 'wc3_movement_modes_probe.j' if args.scenario == 'movement_modes'
                                      else 'wc3_movement_bypasses_probe.j' if args.scenario == 'movement_bypasses'
                                      else 'wc3_region_callbacks_probe.j' if args.scenario == 'region_callbacks'
                                      else 'wc3_movement_lifecycle_probe.j' if args.scenario == 'movement_lifecycle'
                                      else 'wc3_terrain_cache_probe.j' if args.scenario == 'terrain_cache'
                                      else 'wc3_expression_probe.j' if args.scenario == 'chained_expression'
                                      else 'wc3_target_overlap_probe.j' if args.scenario == 'target_overlap'
                                      else 'wc3_adaptive_passage_probe.j' if args.scenario == 'adaptive_passage'
                                      else 'wc3_widget_overlap_probe.j' if args.scenario == 'widget_overlap_orders'
                                      else 'wc3_blocker_lifecycle_probe.j' if args.scenario == 'blocker_lifecycle'
                                      else 'wc3_pathfinding_probe.j').read_text()
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    if members.count('war3map.j') != 1:
        parser.error('source must have exactly one war3map.j')
    if args.scenario in ('numeric_bytes', 'speed_inputs') and members.count('war3map.w3u') != 1:
        parser.error('object probe requires the original unit modification member')
    passage_members=['war3map.w3e','war3map.wpm','war3map.doo','war3mapUnits.doo','war3map.w3u']
    if args.scenario in ('adaptive_passage','target_overlap','chained_expression','terrain_cache','movement_lifecycle','region_callbacks','movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked') and any(members.count(member)!=1 for member in passage_members):
        parser.error('passage requires original terrain, pathing, placement and unit members')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='wc3-path-map-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if args.scenario in ('adaptive_passage','target_overlap','chained_expression','terrain_cache','movement_lifecycle','region_callbacks','movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked'):data=passage_map_member(member,data,[(16,y) for y in range(24)] if args.scenario in ('scheduler_contention','scheduler_mutation','mover_retirement') else None if args.scenario=='adaptive_passage' else [(13,10)] if args.scenario=='formation_blocked' else [] if args.scenario in ('formation_ranks','formation_policy','formation_blocked') else [(12,y)for y in range(24)] if args.scenario in ('movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked') else [],modes=args.scenario=='movement_modes')
            if member=='war3map.w3u' and args.scenario in ('formation_ranks','formation_policy','formation_blocked'):
                data=struct.pack('<III',2,0,4)
                for rank in range(4):
                    code=('hF0'+str(rank)).encode()
                    data+=b'hfoo'+code+struct.pack('<I',1)+b'ufor'+struct.pack('<II',0,rank)+code
            if member=='war3map.w3u' and args.scenario=='fine_results':
                data=resize_units(data,clones=[(b'hF91',8.),(b'hF92',24.),(b'hF93',40.),(b'hF94',56.)])
            if member == 'war3map.j':
                source = data.decode('utf-8').replace('\r\n', '\n')
                data = instrument(source, probe, args.scenario, args.remove_tick, args.gate_y, args.gate_exit_y, args.captain_source_y, args.captain_peer, args.captain_peer_type, args.captain_blocked_home, args.captain_pool, args.captain_third, args.captain_thirteen).encode('utf-8')
                args.output.with_suffix('.j').write_bytes(data)
            if member == 'war3map.w3u' and args.scenario == 'numeric_bytes':
                data = byte_unit_name(data)
                args.output.with_suffix('.w3u').write_bytes(data)
            if member == 'war3map.w3u' and args.scenario == 'speed_inputs':
                data = speed_unit_limits(data)
                args.output.with_suffix('.w3u').write_bytes(data)
            if member == 'war3map.w3u' and args.scenario in ('follow_target_grow','follow_target_shrink','follow_target_resize_gate','moving_radius','moving_radius_matrix','group_radius_grow','group_radius_shrink','group_radius_remove'):
                data=resize_units(data,args.scenario=='moving_radius_matrix')
                args.output.with_suffix('.w3u').write_bytes(data)
            path = root / str(i)
            path.write_bytes(data)
            command.extend([str(path), member])
        if args.scenario in ('formation_ranks','formation_policy','formation_blocked','movement_modes','follow_target_grow','follow_target_shrink','follow_target_resize_gate','moving_radius','moving_radius_matrix','group_radius_grow','group_radius_shrink','group_radius_remove'):
            if 'war3map.w3a' in members: raise ValueError('resize ability table already exists')
            ability=root/'resize.w3a'; ability.write_bytes(resize_ability(args.scenario))
            command.extend([str(ability),'war3map.w3a'])
            args.output.with_suffix('.w3a').write_bytes(ability.read_bytes())
        if args.scenario == 'captain_home':
            command.extend([str(captain_ai), 'Scripts\\wc3_captain_probe.ai'])
            args.output.with_suffix('.ai').write_bytes(captain_ai.read_bytes())
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = {'base_sha256': hashlib.sha256(original).hexdigest(), 'scenario': args.scenario,
              'remove_tick': args.remove_tick,
              'gate_y': args.gate_y, 'gate_exit_y': args.gate_exit_y,
              'map_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(), 'members': members,
              'changed_members': ['war3map.j','war3map.w3u','war3map.w3a'] if args.scenario in ('formation_ranks','formation_policy','formation_blocked','follow_target_grow','follow_target_shrink','follow_target_resize_gate','moving_radius','moving_radius_matrix','group_radius_grow','group_radius_shrink','group_radius_remove') else ['war3map.j', 'war3map.w3u'] if args.scenario in ('numeric_bytes', 'speed_inputs') else ['war3map.j'],
              'script_encoding': 'UTF-8',
              'byte_string_source': 'hfoo unam object field, GetUnitName, raw bytes 80..ff' if args.scenario == 'numeric_bytes' else None,
              'added_members': ['Scripts\\wc3_captain_probe.ai'] if args.scenario == 'captain_home' else ['war3map.w3a'] if args.scenario in ('formation_ranks','formation_policy','formation_blocked') else [],
              'captain_pool': args.captain_pool,
              'captain_third': args.captain_third,
              'captain_thirteen': args.captain_thirteen,
              'captain_peer': args.captain_peer if args.scenario == 'captain_home' else None,
              'captain_peer_type': args.captain_peer_type if args.captain_peer else None,
              'captain_blocked_home': args.captain_blocked_home if args.scenario == 'captain_home' else None,
              'captain_source_y': args.captain_source_y if args.scenario == 'captain_home' else None,
              'captain_ai_sha256': hashlib.sha256(captain_ai.read_bytes()).hexdigest() if args.scenario == 'captain_home' else None,
              'container': 'rebuilt MPQ with original HM3W header; signature not retained'}
    if args.scenario in ('adaptive_passage','target_overlap','chained_expression','terrain_cache','movement_lifecycle','region_callbacks','movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked'):
        result['changed_members']=['war3map.j']+passage_members+(['war3map.w3a'] if args.scenario=='movement_modes' else [])
        result['terrain_blocks']='blocked formation slot (13,10)' if args.scenario=='formation_blocked' else 'empty' if args.scenario in ('formation_ranks','formation_policy') else 'reduced size2 passage' if args.scenario=='adaptive_passage' else 'movement bypass wall' if args.scenario in ('movement_bypasses','movement_modes','speed_modifiers','fine_results','gate_capacity','gate_overlap','scheduler_contention','scheduler_nonunit','scheduler_mutation','mover_retirement','formation_ranks','formation_policy','formation_blocked') else 'empty'
        result['terrain_fixture_sha256']=hashlib.sha256((Path(__file__).resolve().parents[1]/'ghidra/fixtures/pathing-adaptive-size2-passage.json').read_bytes()).hexdigest()
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
