#!/usr/bin/env python3
"""Check original movement profiles, compiled production tables and controlled live witnesses."""
import argparse
import ctypes
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
FIXTURES=HERE/'fixtures'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EXPECTED_SHA='32ee710114a0f3d3e51bd1ad200c0d13e9006c380c4181fd31cfc43633c4b3d5'


def restore_inputs(root):
    bundle=json.loads(gzip.decompress((FIXTURES/'retail-profile-inputs-1.27.json.gz').read_bytes()))
    if set(bundle['files'])!=set(bundle['sha256']):raise ValueError('capture hash inventory differs')
    for name,value in bundle['files'].items():
        path=Path(name)
        if path.is_absolute() or '..' in path.parts:raise ValueError('unsafe input filename')
        raw=value.encode()
        if hashlib.sha256(raw).hexdigest()!=bundle['sha256'][name]:raise ValueError('input changed: '+name)
        output=root/path;output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(raw)
    return bundle


def run(script,*arguments):
    subprocess.run([sys.executable,str(script),*map(str,arguments)],check=True,stdout=subprocess.DEVNULL)


def owner_gate(binary):
    sys.path.insert(0,str(HERE/'research'))
    from verify_base021_movement_types import load_pe
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_EIP
    uc=Uc(UC_ARCH_X86,UC_MODE_32);load_pe(uc,binary,SHA,0x6f000000)
    uc.mem_map(0x20000000,0x10000)
    frame,row=0x20004000,0x20001000
    uc.mem_write(frame-0x28,row.to_bytes(4,'little'))
    result=[]
    def boundary(machine,address,size,data):
        if address in (0x6f68a8de,0x6f68a8fb):machine.emu_stop()
    uc.hook_add(UC_HOOK_CODE,boundary,begin=0x6f68a8de,end=0x6f68a8fb)
    for word in (0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x3f800000,
                 0xbf800000,0x43870000,0x7f7fffff,0xff7fffff,0x7f800000,
                 0xff800000,0x7fc00000,0x7f800001,0xffc12345):
        uc.mem_write(row+0x68,word.to_bytes(4,'little'));uc.reg_write(UC_X86_REG_EBP,frame)
        uc.emu_start(0x6f68a8c4,0x6f68a8fc,count=100)
        pc=uc.reg_read(UC_X86_REG_EIP)
        if pc not in (0x6f68a8de,0x6f68a8fb):raise ValueError('owner gate did not reach boundary')
        creates=pc==0x6f68a8de
        if creates!=(word&0x7fffffff!=0):raise ValueError('unexpected original Move owner gate')
        result.append(dict(speed_bits=word,creates=creates,boundary=f'{pc:08x}'))
    return result


def ordered_markers(rows,count):
    markers=[r['value'] for r in rows if r.get('event') in ('marker','stock-marker')]
    samples=[int(re.search(r'tick=(\d+)',m)[1]) for m in markers
             if m.startswith('PATHTRACE ') and ' label=sample ' in m]
    if samples!=list(range(1,count+1)) or not markers[-1].startswith(f'PATHTRACE tick={count} label=complete '):
        raise ValueError('incomplete ordered capture')
    return markers


def check_control(rows,path,count):
    sys.path.insert(0,str(HERE.parent/'frida'))
    from control_wc3_pathfinding import markers
    expected=ordered_markers(rows,count)
    actual=markers(path.read_text())
    if actual!=expected:raise ValueError('observer-free complete marker comparison differs')
    return len(expected)


def profile_capture(path):
    sys.path.insert(0,str(HERE.parent/'frida/research'))
    import verify_base021_types_trace as trace
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    ends=[r for r in rows if r.get('event')=='trace-end']
    if len(ends)!=1 or not ends[0]['installed'] or ends[0]['units']!=15 or any(
            r.get('event')=='trace-failed' or r.get('type')=='error' for r in rows):
        raise ValueError('incomplete movement-profile capture')
    ordered_markers(rows,150)
    norm=trace.normalize(rows,trace.PROBE_TYPES[:15])
    # Support callbacks sample presentation time. Public markers retain the
    # complete fixed simulation sequence; birth and publication events are ordered.
    return rows,{k:norm[k] for k in ('birth','events','parse')}


def verify(binary):
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=SHA:raise ValueError('retail binary differs')
    frozen=FIXTURES/'research/BASE-02.1-expected.json'
    if hashlib.sha256(frozen.read_bytes()).hexdigest()!=EXPECTED_SHA:raise ValueError('frozen BASE payload differs')
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);bundle=restore_inputs(root)
        fresh=root/'fresh-oracle.json'
        run(HERE/'research/verify_base021_movement_types.py','--binary',binary,'--report',fresh)
        oracle=json.loads(fresh.read_text())
        if oracle!=json.loads((root/'oracle.json').read_text()):raise ValueError('full original parser/table/builder report differs')
        library=root/'profiles.so'
        subprocess.run(['cc','-shared','-fPIC','-O2',str(HERE/'wc3_movement_profile_probe.c'),'-o',str(library)],check=True)
        engine=ctypes.CDLL(str(library));engine.wc3_profile_parse.argtypes=[ctypes.c_char_p]
        engine.wc3_profile_mapping.argtypes=[ctypes.c_uint32,ctypes.c_uint]
        comparisons=1
        if engine.wc3_profile_parse(None)!=oracle['parse_null_ecx']:raise ValueError('null parser differs')
        for row in oracle['parse']:
            if engine.wc3_profile_parse(bytes.fromhex(row['input']))!=row['bits']:raise ValueError('compiled parser differs')
            comparisons+=1
        for row in oracle['lanes']:
            expected=(row['map_edx_0'],row['map_edx_1'],row['published_class'],(2,0x80,0x40,4)[row['published_class']])
            for field,value in enumerate(expected):
                if engine.wc3_profile_mapping(row['bits'],field)!=value:raise ValueError('compiled exact bit switch differs')
                comparisons+=1
        check=root/'old-check.json'
        run(HERE.parent/'frida/research/verify_base021_types_trace.py',root/'types2-first.jsonl',
            '--oracle',fresh,'--compare',root/'types2-repeat.jsonl','--report',check)
        if not json.loads(check.read_text())['identical']:raise ValueError('complete research repeat differs')
        rebuilt=root/'rebuilt-BASE.json'
        run(HERE.parent/'frida/research/base021_freeze_expected.py','--oracle',fresh,
            '--capture',root/'types2-first.jsonl','--repeat',root/'types2-repeat.jsonl','--output',rebuilt)
        if rebuilt.read_bytes()!=frozen.read_bytes():raise ValueError('complete original frozen BASE evidence differs')
        old_rows=[json.loads(line) for line in (root/'types2-first.jsonl').read_text().splitlines()]
        old_markers=check_control(old_rows,root/'types2-control.txt',300)
        rows,first=profile_capture(root/'profiles-first.jsonl')
        _,repeat=profile_capture(root/'profiles-repeat.jsonl')
        if first!=repeat:raise ValueError('complete movement profile repeat differs')
        new_markers=check_control(rows,root/'profiles-control.txt',150)
        if first!=json.loads((root/'profiles-normalized.json').read_text()):raise ValueError('full frozen movement events differ')
        for birth in first['birth']:
            text=trace_name(birth['type']);p=next(r for r in oracle['parse'] if r['text']==text)
            lane=next(r for r in oracle['lanes'] if r['bits']==p['bits'])
            want=dict(index=birth['index'],type=birth['type'],class_flags=lane['class_flags'],
                profile=[lane['map_edx_1'],lane['map_edx_0']],profile_count=2,adaptive=1)
            if birth!=want:raise ValueError('live birth profile differs')
        # A zero fine query is independent of whether a positive-speed Move
        # owner accepts and executes a command. Keep every probe unit in the
        # comparison; no per-type trajectory constants select a passing case.
        states={};orders={}
        for marker in ordered_markers(rows,150):
            if not marker.startswith('PATHSTOCK '):continue
            fields=dict(part.split('=',1) for part in marker.split()[1:])
            if fields['label']=='state':states[(int(fields['tick']),int(fields['i']))]=fields
            elif fields['label']=='order':orders[int(fields['i'])]=fields
        zero_query_movers=0
        for birth in first['birth']:
            if birth['profile'][1]:continue
            index=birth['index'];before=states[(9,index)];after=states[(150,index)]
            if float(before['speed'])>0:
                if orders[index]['accepted']!='1' or (before['x'],before['y'])==(after['x'],after['y']):
                    raise ValueError('positive-speed zero-query owner failed to move')
                zero_query_movers+=1
        gate=owner_gate(binary)
        if gate!=json.loads((root/'owner-gate.json').read_text()):raise ValueError('full Move owner gate differs')
        return dict(binary_sha256=SHA,status='verified',differences=[],cases=comparisons+len(gate),
            original=oracle['counts'],live_births=39,live_captures=4,observer_free_captures=2,
            research_markers=old_markers,movement_markers=new_markers,movement_events=len(first['events']),
            owner_gate=gate,zero_query_movers=zero_query_movers,input_files=len(bundle['files']))


def trace_name(rawcode):
    sys.path.insert(0,str(HERE.parent/'frida/research'))
    from verify_base021_types_trace import UMVT
    return UMVT[rawcode]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();result=verify(args.binary.resolve())
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
