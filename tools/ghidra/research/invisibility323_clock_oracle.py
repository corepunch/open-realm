#!/usr/bin/env python3
"""Original Binv clock counters/refresh, with explicitly bounded timer services."""
import argparse
import copy
import json
import struct
from pathlib import Path
from foot03_spatial_harness_copy import Emu
from item_invisibility322_refresh_oracle import byte_guards

FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/research/invisibility323-original.json'
def word(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]

def expected():
    # The timer service supplies eight seconds remaining, two elapsed.
    # These are fixture inputs, not claimed observed game-clock values.
    return [
        ('first_add', 1, False, 8, 2, ['remaining', 'scalar', 'elapsed', 'scalar']),
        ('nested_add', 2, False, 8, 2, []),
        ('nonlast_remove', 1, False, 8, 2, []),
        ('paused_refresh', 1, True, 8, 0, ['v1b4', 'v1c8', 'context', 'selector', 'scalar', 'rearm']),
        ('last_remove', 0, True, 0, 0, ['scalar', 'rearm', 'scalar']),
        ('inherited_three', 3, False, 8, 2, ['remaining', 'scalar', 'elapsed', 'scalar']),
        ('inherited_remove_two', 1, False, 8, 2, []),
        ('inherited_last', 0, True, 0, 0, ['scalar', 'rearm', 'scalar']),
    ]

def validate(rows):
    assert len(rows) == len(expected())
    for row, (name, depth, active, frozen, elapsed, calls) in zip(rows, expected()):
        assert row['case'] == name and row['depth'] == depth and row['active'] is active
        assert row['frozen_word'] == word(frozen) and row['elapsed_word'] == word(elapsed)
        assert row['calls'] == calls
        assert row['stack_delta'] == (12 if name == 'paused_refresh' else 8)
        assert row['rearm_word'] == (word(3 if name == 'paused_refresh' else 8) if 'rearm' in calls else None)

def negative_controls(rows):
    mutations = []
    x=copy.deepcopy(rows); x.pop(); mutations.append(x)
    x=copy.deepcopy(rows); x.append(x[-1]); mutations.append(x)
    x=copy.deepcopy(rows); x[0],x[1]=x[1],x[0]; mutations.append(x)
    for i, key, value in [(1,'depth',1),(0,'active',True),(3,'frozen_word',0),
                          (4,'rearm_word',word(3)),(4,'frozen_word',word(8)),
                          (2,'calls',['rearm'])]:
        x=copy.deepcopy(rows); x[i][key]=value; mutations.append(x)
    for x in mutations:
        try: validate(x)
        except AssertionError: continue
        raise AssertionError('mutation accepted')
    return len(mutations)

def run(binary):
    from unicorn import UC_HOOK_CODE
    e=Emu(binary); X=e.X
    a,v,tv,sv,request,duration,context=[e.fixture(n) for n in (0x100,0x400,0x30,4,0x40,4,32)]
    e.w(a,v); e.w(a+0x84,tv); e.w(a+0xa0,sv); e.w(a+0xa8,sv)
    for off, addr in [(0xe8,0x6f531420),(0xec,0x6f532fa0)]: e.w(v+off,addr)
    e.w(tv+0x18,0x6f060ad0)
    names=['elapsed','scalar','v1b4','v1c8','context','selector','rearm']
    stubs={e.fixture(16):name for name in names}; addresses={name:pc for pc,name in stubs.items()}
    e.w(tv+0x1c,addresses['elapsed']); e.w(sv,addresses['scalar'])
    for off,name in [(0x1b4,'v1b4'),(0x1c8,'v1c8'),(0x324,'context'),(0x348,'selector'),(0x34c,'rearm')]:
        e.w(v+off,0x6f532b50 if name=='rearm' else addresses[name])
    calls=[]; rearms=[]
    def hook(uc,pc,size,data):
        sp=uc.reg_read(X.UC_X86_REG_ESP); cx=uc.reg_read(X.UC_X86_REG_ECX)
        name='remaining' if pc==0x6f060a90 else 'rearm' if pc==0x6f0609b0 else stubs[pc]
        calls.append(name); argc=0; ret=0
        if name in ('remaining','elapsed'):
            assert cx==a+0x84
            ret=e.r(sp+4); e.w(ret,word(8 if name=='remaining' else 2)); argc=1
        elif name=='scalar':
            assert cx in (a+0xa0,a+0xa8)
            e.w(cx+4,e.r(e.r(sp+4))); argc=2
        elif name=='context':
            assert cx==a and e.r(sp+4)==context; argc=1
        elif name=='selector': ret=0
        elif name=='rearm':
            assert cx==a+0x84 and e.r(sp+8)==0xd01c4 and e.r(sp+12)==a
            assert e.r(sp+16,2)==[0,0]
            rearms.append(e.r(e.r(sp+4)))
            # External0609b0 heap scheduling service only; original34c already
            # canceled the old request and zeroed elapsed. No real heap.
            e.w(a+0x90,request); e.w(request+0x10,0); argc=5
        uc.reg_write(X.UC_X86_REG_EAX,ret)
        uc.reg_write(X.UC_X86_REG_ESP,sp+4+4*argc)
        uc.reg_write(X.UC_X86_REG_EIP,e.r(sp))
    for pc in [*stubs,0x6f060a90,0x6f0609b0]: e.uc.hook_add(UC_HOOK_CODE,hook,begin=pc,end=pc)
    e.w(a+0x90,request); e.w(duration,word(3)); rows=[]
    operations=[('first_add',0x6f427610,1),('nested_add',0x6f427610,1),
                ('nonlast_remove',0x6f43b0e0,1),('paused_refresh',0x6f531a80,None),
                ('last_remove',0x6f43b0e0,1),('inherited_three',0x6f427610,3),
                ('inherited_remove_two',0x6f43b0e0,2),('inherited_last',0x6f43b0e0,1)]
    for name,pc,delta in operations:
        calls.clear(); rearms.clear()
        args=(context,duration) if delta is None else (delta,)
        e.call(pc,a,*args,budget=10000)
        rows.append(dict(case=name,depth=e.r(a+0x48),active=bool(e.r(a+0x90)),
                         frozen_word=e.r(a+0xac),elapsed_word=e.r(a+0xa4),calls=calls[:],
                         rearm_word=rearms[-1] if rearms else None,stack_delta=e.esp_after))
    validate(rows)
    return rows

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path)
    p.add_argument('--check',action='store_true')
    args=p.parse_args(); spec=json.loads(FIXTURE.read_text())
    validate(spec['rows']); n=negative_controls(spec['rows'])
    if args.binary:
        byte_guards(args.binary,spec)
        assert run(args.binary)==spec['rows']
    print(json.dumps(dict(passed=True,cases=len(spec['rows']),negative_controls=n,
                          original_executed=bool(args.binary),scope=spec['scope'])))
if __name__=='__main__': main()
