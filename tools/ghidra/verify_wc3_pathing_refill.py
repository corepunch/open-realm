#!/usr/bin/env python3
"""Compose original fine-route refill, admission, search and indexed output.

No stubs or retail bytes. Synthetic preallocated maps; compares path-owned
refill with the complete underlying request from identical initial state.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import itertools
import ctypes


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine',type=Path,help='compare the production search obstruction latch with original requests')
    parser.add_argument('--adaptive-handoff-fixture',type=Path,help='export complete original coarse-selected fine routes on 64-cell maps')
    parser.add_argument('--adaptive-handoff-reference',type=Path,help='compare coarse-selected fine routes with frozen original words')
    parser.add_argument('--adaptive-progress-fixture',type=Path,help='export admitted repeated original coarse/fine advances at controlled coarse waypoints')
    parser.add_argument('--adaptive-progress-reference',type=Path,help='repeat controlled original coarse approach/refill transitions against frozen words')
    parser.add_argument('--adaptive-long-fixture',type=Path,help='export repeated nonzero coarse indices and fine refills on 128-cell maps')
    parser.add_argument('--adaptive-long-reference',type=Path,help='compare complete long-route refills against frozen original words')
    parser.add_argument('--unit-budget-fixture',type=Path,help='export700/2048-attempt fine requests on a winding64-cell map')
    parser.add_argument('--unit-budget-reference',type=Path,help='compare original winding partial-route budget outcomes')
    parser.add_argument('--unit-route-fixture',type=Path,help='export enabled default400/700 full route composition on winding64-cell maps')
    parser.add_argument('--unit-route-reference',type=Path,help='compare original default adaptive/fine maze handoffs')
    parser.add_argument('--public-results-fixture',type=Path,help='export same-cell/blocked/partial/special-target full fine caller results')
    parser.add_argument('--public-results-reference',type=Path,help='compare full fine caller results with frozen original words')
    parser.add_argument('--cached-route-fixture',type=Path,help='export full advance over cached/exhausted fine routes, disabled gate and wait precedence')
    parser.add_argument('--cached-route-reference',type=Path,help='compare cached/exhausted full-advance state words')
    args = parser.parse_args()
    engine=ctypes.CDLL(str(args.engine.resolve())) if args.engine else None
    class FineInput(ctypes.Structure):
        _fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]
    if engine:
        engine.pathing_fine_obstruction.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(FineInput)]
        engine.pathing_fine_obstruction.restype=ctypes.c_uint32
    engine_obstruction_cases=0
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])

    machine.mem_map(0x10000000, 0x200000)
    machine.mem_map(0x20000000, 0x10000)
    system, tilemap, cells, bitmap = 0x10000000, 0x10000200, 0x10001000, 0x10002000
    nodes, links, heap = 0x10010000, 0x10020000, 0x10030000
    stack, stop = 0x20008000, 0x30000000
    route, route_data = 0x10100000, 0x10101000
    source_ptr, target_ptr, mask_ptr, radius_ptr = 0x10000400, 0x10000410, 0x10000420, 0x10000430
    for address, value in ((0x6fd3c740, -1.0), (0x6fd3c744, 0.0), (0x6fd3c748, 1.0)):
        machine.mem_write(address, struct.pack('<f', value))
    width = height = 24
    start, goal = (4, 4), (19, 19)

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def check_obstruction(source_value,target_value):
        nonlocal engine_obstruction_cases
        if not engine:return
        grid=(ctypes.c_uint8*(width*height))(*(2 if (x,y) in blocked else 0 for y in range(height) for x in range(width)))
        q=(ctypes.c_uint32*11)(width,height,int(source_value[0]),int(source_value[1]),
            int(target_value[0]),int(target_value[1]),700,cls,0x02000000,0,0)
        actual=engine.pathing_fine_obstruction(q,ctypes.byref(FineInput(grid,None)))
        assert actual==read(system+0xd0)[0],(width,name,cls,source_value,target_value,'obstruction',actual,read(system+0xd0)[0])
        engine_obstruction_cases+=1

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=20000000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail grid search exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    machine.mem_map(0,0x1000)
    path, owner, acc, accmap, coarse, output = [0x10110000+n for n in (0,0x1000,0x2000,0x3000,0x4000,0x5000)]
    write(0x6fd53a48,owner)
    write(owner+0x24c,system,acc)
    write(owner+0x538,100)
    write(acc+0x1c,accmap)
    machine.mem_write(accmap+0x64,struct.pack('<f',2))
    bucket=0x6fd53a90+0x54
    fixtures=[('open',set()),('wall',{(12,y) for y in range(height)}),
              ('gap',{(12,y) for y in range(height) if not 9<=y<=14})]
    records=[]
    static_routes={}
    denied_cases=cached_cases=0
    for (name,blocked),cls,budget,use_coarse in itertools.product(fixtures,range(4),[0,5,700],[False,True]):
        source=(4.25,4.75)
        target=(19.25,19.75)
        def setup():
            machine.mem_write(system,bytes(0x400))
            machine.mem_write(bitmap,bytes(128))
            machine.mem_write(cells,b''.join(struct.pack('<I',0x02ffffff if (x,y) in blocked else 0xffffff)
                                             for y in range(height) for x in range(width)))
            write(system+0x1c,tilemap,1)
            write(system+0x30,nodes)
            write(system+0x3c,4096,0)
            write(system+0x50,heap)
            write(system+0x5c,32768,1,-3,100000,0)
            write(tilemap+0x28,cells)
            write(tilemap+0x3c,width,height)
            write(tilemap+0x78,links)
            write(tilemap+0x84,8192,0)
            write(tilemap+0x98,bitmap)
            write(tilemap+0xac,0xffffff)
            machine.mem_write(source_ptr,struct.pack('<2f',*source))
            machine.mem_write(target_ptr,struct.pack('<2f',*target))
            machine.mem_write(radius_ptr,struct.pack('<f',.25+.5*cls))
            write(mask_ptr,0x02000000)
            write(route+0xc,route_data)
            write(route+0x18,1024,0)
        setup()
        direct=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,budget,radius_ptr,0)
        count=read(route+0x1c)[0]
        expected=bytes(machine.mem_read(route_data,count*8))
        pops=read(system+0x6c)[0]
        reset_index=bool(read(system+0xd0)[0])
        setup()
        machine.mem_write(path,bytes(0x100))
        machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
        write(path+0x40,route_data)
        write(path+0x4c,1024,0)
        write(path+0x60,coarse)
        write(path+0x70,2 if use_coarse else 0,-1,1 if use_coarse else 0)
        write(path+0x84,budget | 400<<16)
        write(path+0x9c,0x02000000)
        machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
        machine.mem_write(coarse+8,struct.pack('<2f',target[0]/2,target[1]/2))
        # Empty admissible scheduler bucket; request sets its own class flag.
        write(bucket,700 | 1<<16,1100,0,0,0,0,0)
        write(output,0xdeadbeef,0xdeadbeef)
        result=run(0x6f167ce0,path,source_ptr,output)
        assert result==1
        assert read(path+0x50)[0]==count
        assert bytes(machine.mem_read(route_data,count*8))==expected
        index=(count-2 if count>1 else 0) if reset_index else 0
        assert read(path+0x74)[0]==index
        assert bytes(machine.mem_read(output,8))==expected[index*8:index*8+8]
        assert read(bucket+8)[0]==pops
        assert read(path+0x7c)[0]==(0 if pops<64 else 100)
        endpoint=struct.unpack('<2f',expected[:8])
        assert bool(read(path+0x88)[0]&0x10000000)==(endpoint!=target)
        assert read(path+0x88)[0]&0x2000000
        assert read(0)[0]==0
        # A valid cached index bypasses interval/admission and all search work.
        write(path+0x74,0)
        write(path+0x7c,100)
        work_before=read(bucket+8)[0]
        write(output,0xdeadbeef,0xdeadbeef)
        assert run(0x6f167ce0,path,source_ptr,output)==1
        assert bytes(machine.mem_read(output,8))==expected[:8]
        assert read(path+0x7c)[0]==100 and read(bucket+8)[0]==work_before
        cached_cases+=1
        for denial in ['interval','budget']:
            write(path+0x50,0)
            write(path+0x74,-1)
            write(path+0x7c,100 if denial=='interval' else 0)
            write(path+0x8c,0,0)
            write(bucket,700 | 1<<16,1100,1101 if denial=='budget' else 0,0,0,0,0)
            before=bytes(machine.mem_read(system,0x100))
            write(output,0xdeadbeef,0xdeadbeef)
            assert run(0x6f167ce0,path,source_ptr,output)==0
            assert read(output,2)==[0xdeadbeef]*2
            assert read(path+0x50)[0]==0 and read(path+0x74)[0]==0xffffffff
            assert bytes(machine.mem_read(system,0x100))==before
            assert read(path+0x7c)[0]==(100 if denial=='interval' else 0)
            assert read(bucket+0x10)[0]==(1 if denial=='budget' else 0)
            if denial=='budget':
                assert read(bucket+0x14)[0]==path
            denied_cases+=1
        static_routes[name,cls,budget]=(expected,index,pops)
        records.append(dict(fixture=name,size_class=cls,budget=budget,coarse=use_coarse,
                            underlying_result=direct,refill_result=result,count=count,index=index,pops=pops,endpoint=endpoint))
    # Live object records on a full-height wall, with actual query stamping.
    # Compare the wrapper with explicit expected suppression in the underlying
    # request, and observe its counter at entry without replacing any code.
    obj=0x10118000
    observed=[]
    def observe(uc,address,size,data):
        observed.append(read(obj+0x40)[0])
    hook=machine.hook_add(UC_HOOK_CODE,observe,begin=0x6f148100,end=0x6f148100)
    machine.ctl_flush_tb()  # install observer after earlier translated calls
    dynamic=[]
    terrain_equivalence=0
    blocked=set()
    for cls,budget,role,prior in itertools.product(range(4),[0,5,700],
            ['none','self','target','both'],[0,1,0x20000000,0x40000000,0x80000000,0xffffffff]):
        increments=(role in ['self','both'])+(role in ['target','both'])
        target_obj=obj if role in ['target','both'] else 0
        during=(prior+increments)&0xffffffff
        def setup_object(counter):
            setup()
            write(mask_ptr,1)
            machine.mem_write(obj,bytes(0x80))
            write(obj+0x34,0x01000001,0)
            write(obj+0x40,counter)
            write(links,0x01ffffff,obj)
            write(tilemap+0x88,1)
            for y in range(height):
                write(cells+(y*width+12)*4,0)
        setup_object(during)
        direct=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,budget,radius_ptr,target_obj)
        count=read(route+0x1c)[0]
        expected=bytes(machine.mem_read(route_data,count*8))
        pops=read(system+0x6c)[0]
        reset_index=bool(read(system+0xd0)[0])
        setup_object(prior)
        machine.mem_write(path,bytes(0x100))
        machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
        write(path+0x40,route_data)
        write(path+0x4c,1024,0)
        write(path+0x74,-1,0)
        write(path+0x84,budget | 400<<16)
        write(path+0x9c,1,obj if role in ['self','both'] else 0,target_obj)
        machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
        write(bucket,700 | 1<<16,1100,0,0,0,0,0)
        observed.clear()
        result=run(0x6f167ce0,path,source_ptr,output)
        assert result==1
        assert observed==[during],(role,prior,observed)
        assert read(obj+0x40)[0]==prior
        assert read(path+0x50)[0]==count
        assert bytes(machine.mem_read(route_data,count*8))==expected,(cls,budget,role,prior)
        index=(count-2 if count>1 else 0) if reset_index else 0
        assert read(path+0x74)[0]==index
        assert bytes(machine.mem_read(output,8))==expected[index*8:index*8+8]
        assert read(bucket+8)[0]==pops
        assert read(0)[0]==0
        endpoint=struct.unpack('<2f',expected[:8])
        assert bool(read(path+0x88)[0]&0x10000000)==(endpoint!=target)
        if not target_obj:
            reference_kind='wall' if during==0 else 'open'
            assert (expected,index,pops)==static_routes[reference_kind,cls,budget]
            terrain_equivalence+=1
        elif budget==700 and prior==0:
            # Perimeter offset is1 for classes0/1 and2 for classes2/3.
            wanted=11.5 if cls<2 else 10.5
            assert direct==1 and endpoint==(wanted,wanted)
        dynamic.append(dict(size_class=cls,budget=budget,role=role,prior_flags=prior,
                            during_flags=during,count=count,index=index,pops=pops,
                            underlying_result=direct,endpoint=struct.unpack('<2f',expected[:8])))
    machine.hook_del(hook)
    # Public advance: build a one-point accelerator route when disabled,
    # refill fine route, then execute next-step or terminal retry behavior.
    mover,group,registry,slots,candidates=[0x10190000+n for n in (0,0x200,0x400,0x500,0x1000)]
    write(0x6fd68610,registry)
    write(registry+0xc,slots)
    write(registry+0x1c,2)
    for n,item in enumerate([mover,group]):
        write(slots+n*8,-2,item)
        write(item+0x14,n,100+n)
    write(mover+0x9c,1,101)
    write(mover+0xa8,path)
    write(group+0x38,1)
    machine.mem_write(accmap+0x68,struct.pack('<f',0.5))
    machine.mem_write(0x6fd54190,struct.pack('<f',144))
    full_advance=[]
    for (name,blocked),cls,budget,acc_state in itertools.product(fixtures,range(4),[0,5,700],
            ['disabled_empty','existing_final','existing_segment']):
        setup()
        write(system+0xac+0xc,candidates)
        write(system+0xac+0x18,64,0)
        machine.mem_write(path,bytes(0x100))
        machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
        write(path+0x40,route_data)
        write(path+0x4c,1024,0)
        write(path+0x60,coarse)
        write(path+0x6c,1024,0,-1,-1)
        write(path+0x84,budget | 400<<16)
        if acc_state!='disabled_empty':
            write(path+0x70,2 if acc_state=='existing_segment' else 1,-1,
                  1 if acc_state=='existing_segment' else 0)
            write(path+0x88,0x200000)
            machine.mem_write(coarse,struct.pack('<4f',target[0]/2,target[1]/2,target[0]/2,target[1]/2))
        write(path+0x9c,0x02000000)
        write(path+0xa8,-1,-1)
        machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
        write(bucket,700 | 1<<16,1100,0,0,0,0,0)
        write(owner,0,0)
        machine.mem_write(output,struct.pack('<2f',*target))
        result=run(0x6f165ae0,path,source_ptr,output,mover)
        segment=acc_state=='existing_segment'
        wanted_result=(2 if segment else 1) if budget==0 else 0
        assert result==wanted_result,(name,cls,budget,acc_state,result)
        assert read(path+0x70)[0]==(2 if segment else 1)
        assert read(path+0x78)[0]==(1 if segment and budget else 0)
        assert struct.unpack('<2f',machine.mem_read(coarse,8))==(target[0]/2,target[1]/2)
        assert read(0x6fd53a84,3)==[system,tilemap,mover]
        assert struct.unpack('<H',machine.mem_read(0x6fd53a80,2))[0]==cls
        expected,index,pops=static_routes[name,cls,budget]
        assert read(bucket+8)[0]==pops
        if budget==0:
            assert read(path+0x50)[0]==(1 if segment else 0)
            assert read(path+0x74)[0]==0xffffffff
            assert read(path+0x98)[0] in ([0] if segment else [6,7])
            assert struct.unpack('<2f',machine.mem_read(output,8))==(source if segment else target)
        else:
            assert read(path+0x50)[0]==len(expected)//8
            assert read(path+0x74)[0]==index
            assert bytes(machine.mem_read(output,8))==expected[index*8:index*8+8]
            assert read(path+0x98)[0]==0
        assert read(path+0x94)[0]==0
        assert read(0)[0]==0
        full_advance.append(dict(fixture=name,size_class=cls,budget=budget,acc_state=acc_state,result=result,
                                 retries=read(path+0x98)[0],fine_count=read(path+0x50)[0]))
    cached_route_cases=[]
    if args.cached_route_fixture or args.cached_route_reference:
        # Cache validity is unsigned index<count, including count1. Use the
        # complete165ae0 caller with actual context, step collector and registry.
        # Supplied far-source states establish consumption, not a public lifetime
        # capable of creating each state; the live producer is reported separately.
        source=(4.25,4.75); target=(19.25,19.75); blocked=set(); budget=700
        states=[(1,0),(2,0),(2,1),(5,0),(5,3),(5,4),
                (0,-1),(0,0),(1,-1),(1,1),(2,-1),(2,2)]
        for cls,(count,index),enabled,disabled,delay in itertools.product(
                range(4),states,[False,True],[False,True],[0,1,4]):
            setup()
            write(system+0xac+0xc,candidates);write(system+0xac+0x18,64,0)
            machine.mem_write(path,bytes(0x100))
            machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
            write(path+0x40,route_data);write(path+0x4c,1024,count)
            write(path+0x60,coarse);write(path+0x6c,1024,1,index,0)
            machine.mem_write(coarse,struct.pack('<2f',target[0]/2,target[1]/2))
            # Each cached point differs from the held goal and predicted source.
            machine.mem_write(route_data,struct.pack('<'+'f'*10,*[v for n in range(5)for v in (15.5+n*.25,12.5+n*.125)]))
            write(path+0x84,700|400<<16)
            write(path+0x88,(0x200000 if enabled else 0)|(0x100000 if disabled else 0))
            write(path+0x94,delay,0);write(path+0x9c,0x02000000);write(path+0xa8,-1,-1)
            machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
            # Exhausted routes cannot obtain another fine request in this visit.
            write(bucket,700|1<<16,1100,1101,0,0,0,0);write(owner,0,0)
            machine.mem_write(output,struct.pack('<2f',*target))
            before=read(path+0x50)[0],read(path+0x74)[0],read(path+0x94)[0]
            result=run(0x6f165ae0,path,source_ptr,output,mover)
            valid=0<=index<count
            expected=0x100000 if disabled else 1 if delay else 0 if valid else 2
            assert result==expected,(cls,count,index,enabled,disabled,delay,result)
            assert read(path+0x94)[0]==(delay if disabled else max(0,delay-1))
            assert (read(path+0x50)[0],read(path+0x74)[0])==before[:2]
            wanted=read(route_data+index*8,2) if valid and not disabled and not delay else read(target_ptr,2)
            assert read(output,2)==wanted
            assert read(bucket+8)[0]==1101 # no search work in any row
            cached_route_cases.append(dict(cls=cls,count=count,index=index,adaptive_enabled=enabled,
                disabled=disabled,delay=delay,source=read(source_ptr,2),goal=read(target_ptr,2),
                points=read(route_data,count*2),result=result,output=read(output,2),
                after=dict(count=read(path+0x50)[0],index=read(path+0x74)[0],delay=read(path+0x94)[0],
                           fine_work=read(bucket+8)[0],fine_waiting=read(bucket+0x10)[0])))
        payload=dict(version=1,binary_sha256=digest,entry='6f165ae0',cases=cached_route_cases,
            scope='Complete original advance, query context, accelerator progress, fine cache/denial and next-step collector. Synthetic preallocated24-cell open map, cached adaptive index0, denied fine bucket. All four footprint classes. Far-source cached one-point state is controlled; public production/reachability and terminal/yield/warp lifetimes are not certified.')
        if args.cached_route_fixture:args.cached_route_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        if args.cached_route_reference:assert payload==json.loads(args.cached_route_reference.read_text())
    # Enabled accelerator search composed with public advance. Supplied base
    # classifications, original parent reducer, then both original searches.
    machine.mem_map(0x10400000,0x200000)
    maps=[0x10400000+n*0x100 for n in range(4)]
    storage=[0x10410000+n*0x10000 for n in range(4)]
    acc_nodes,acc_heap,coarse,acc_source,acc_target=0x10480000,0x10500000,0x10580000,0x10590000,0x10590010
    machine.mem_write(0x6fd53a74,struct.pack('<f',-128000.0078125))
    run(0x6f0040d0,0)
    machine.mem_map(0x10800000,0x200000)
    cells,bitmap,nodes,links,heap,route_data=0x10800000,0x10810000,0x10820000,0x10880000,0x10900000,0x10980000
    enabled=[]
    handoff_cases=[]
    progress_cases=[]
    long_cases=[]
    long_inputs=list(itertools.product([128],['open','gap'],range(4),[400],range(4))) if args.adaptive_long_fixture or args.adaptive_long_reference else []
    for map_size,name,cls,acc_budget,lane in long_inputs+list(itertools.product([24,64],['open','wall','gap'],range(4),[0,5,400],range(4))):
        width=height=map_size
        target=(map_size-4.75,map_size-4.25)
        blocked=set() if name=='open' else {(width//2,y) for y in range(height)
                  if name=='wall' or not height//2-3<=y<=height//2+2}
        acc_side=16 if map_size==24 else map_size//2
        budget=700
        def setup_enabled():
            setup()
            write(owner+0x538,100)
            write(system+0xac+0xc,candidates)
            write(system+0xac+0x18,64,0)
            machine.mem_write(acc,bytes(0x400))
            for level,(tilemap_acc,data_acc) in enumerate(zip(maps,storage)):
                side=acc_side>>level
                machine.mem_write(tilemap_acc,bytes(0x100))
                machine.mem_write(data_acc,bytes(side*side*8))
                write(tilemap_acc+0x28,data_acc)
                write(tilemap_acc+0x3c,side,side)
                machine.mem_write(tilemap_acc+0x64,struct.pack('<2f',2<<level,1/(2<<level)))
                write(acc+0x1c+4*level,tilemap_acc)
                for y in range(side):
                    for x in range(side):
                        cell=data_acc+(y*side+x)*8
                        if level==0:
                            children=[(2*x+dx>=width or 2*y+dy>=height or (2*x+dx,2*y+dy) in blocked)
                                      for dy in range(2) for dx in range(2)]
                            code=1 if all(children) else 2 if any(children) else 0
                            write(cell+4,code*0x55000000)
                        else:
                            for shift in [0,2,4,6]:
                                run(0x6f15d1c0,0,cell,maps[level-1],shift,2*x,2*y)
            write(acc+0x5c,acc_nodes)
            write(acc+0x68,4096,0)
            write(acc+0x7c,acc_heap)
            write(acc+0x88,65536,0)
            machine.mem_write(path,bytes(0x100))
            machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
            write(path+0x40,route_data)
            write(path+0x4c,1024,0)
            write(path+0x60,coarse)
            write(path+0x6c,1024,0,-1,-1)
            write(path+0x84,700 | acc_budget<<16)
            write(path+0x88,0x200000 | lane<<30)
            write(path+0x9c,0x02000000)
            write(path+0xa8,-1,-1)
            machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
            write(bucket,700 | 1<<16,1100,0,0,0,0,0)
            write(0x6fd53a90+0x38,400 | 2<<16,900,0,0,0,0,0)
            write(owner,0,0)
            machine.mem_write(acc_source,struct.pack('<2f',source[0]/2,source[1]/2))
            machine.mem_write(acc_target,struct.pack('<2f',target[0]/2,target[1]/2))
            machine.mem_write(output,struct.pack('<2f',*target))
        setup_enabled()
        assert run(0x6f166c30,path,acc_source,acc_target,1)==1
        count=read(path+0x70)[0]
        coarse_bytes=bytes(machine.mem_read(coarse,count*8))
        adjusted=read(path+0x24,2)
        acc_pops=read(acc+0x9c)[0]
        mismatch=read(path+0x88)[0]&0x20000000
        setup_enabled()
        result=run(0x6f165ae0,path,source_ptr,output,mover)
        assert result==0,(name,cls,acc_budget,lane,result)
        acc_index=read(path+0x78)[0]
        fine_count=read(path+0x50)[0]
        actual_fine=bytes(machine.mem_read(route_data,fine_count*8))
        actual_index=read(path+0x74)[0]
        actual_output=bytes(machine.mem_read(output,8))
        actual_pops=read(system+0x6c)[0]
        actual_obstruction=read(system+0xd0)[0]
        fine_goal=target if acc_index==0 else tuple(v*2 for v in struct.unpack('<2f',coarse_bytes[acc_index*8:acc_index*8+8]))
        check_obstruction(source,fine_goal)
        if map_size==24:
            assert acc_index==0
            fine_bytes,fine_index,fine_pops=static_routes[name,cls,700]
            assert (actual_fine,actual_index,actual_pops)==(fine_bytes,fine_index,fine_pops)
        assert actual_output==actual_fine[actual_index*8:actual_index*8+8]
        assert struct.unpack('<2f',machine.mem_read(path+0x1c,8))==target
        assert read(path+0x70)[0]==count
        assert bytes(machine.mem_read(coarse,count*8))==coarse_bytes
        assert read(path+0x24,2)==adjusted
        assert read(acc+0x9c)[0]==acc_pops
        assert read(0x6fd53a90+0x38+8)[0]==acc_pops
        assert read(path+0x88)[0]&0x20000000==mismatch
        assert read(0)[0]==0
        enabled.append(dict(map_size=map_size,fixture=name,size_class=cls,acc_budget=acc_budget,lane=lane,
                            result=result,acc_count=count,acc_index=read(path+0x78)[0],acc_pops=acc_pops,
                            fine_count=read(path+0x50)[0],fine_pops=read(system+0x6c)[0],
                            adjusted=struct.unpack('<2f',struct.pack('<2I',*adjusted)),
                            fine_goal=fine_goal,output=struct.unpack('<2f',actual_output)))
        if map_size==64 and acc_budget==400:
            handoff_cases.append(dict(fixture=name,size_class=cls,lane=lane,
                source_bits=list(struct.unpack('<2I',struct.pack('<2f',*source))),
                goal_bits=list(struct.unpack('<2I',struct.pack('<2f',*target))),
                coarse_count=count,coarse_index=acc_index,coarse_words=list(struct.unpack('<'+'I'*(count*2),coarse_bytes)),
                fine_goal_bits=list(struct.unpack('<2I',struct.pack('<2f',*fine_goal))),
                fine_count=fine_count,fine_words=list(struct.unpack('<'+'I'*(fine_count*2),actual_fine))))
        if (map_size==128 or ((args.adaptive_progress_fixture or args.adaptive_progress_reference) and map_size==64)) and acc_budget==400 and name!='wall':
            steps=[]
            prior_index=acc_index
            for step in range(1,8):
                if not prior_index:break
                current_point=struct.unpack('<2f',coarse_bytes[prior_index*8:prior_index*8+8])
                source_words=struct.unpack('<2I',struct.pack('<2f',current_point[0]*2-.96,current_point[1]*2))
                write(source_ptr,*source_words)
                write(owner+0x538,100+10*step)
                write(bucket+8,0)
                machine.mem_write(output,struct.pack('<2f',*target))
                advanced=run(0x6f165ae0,path,source_ptr,output,mover)
                next_index=read(path+0x78)[0]
                n=read(path+0x50)[0]
                next_goal=target if next_index==0 else tuple(v*2 for v in struct.unpack('<2f',coarse_bytes[next_index*8:next_index*8+8]))
                check_obstruction(struct.unpack('<2f',struct.pack('<2I',*source_words)),next_goal)
                steps.append(dict(source_bits=list(source_words),result=advanced,coarse_index=next_index,
                    fine_count=n,fine_index=read(path+0x74)[0],fine_words=read(route_data,n*2),
                    fine_work=read(bucket+8)[0],fine_timestamp=read(path+0x7c)[0],delay=read(path+0x94)[0]))
                if map_size==128:steps[-1]['observed_obstruction']=read(system+0xd0)[0]
                assert advanced==0 and next_index<prior_index,(name,cls,lane,step,steps[-1])
                assert read(path+0x70)[0]==count and bytes(machine.mem_read(coarse,count*8))==coarse_bytes
                prior_index=next_index
            assert prior_index==0
            record=dict(fixture=name,size_class=cls,lane=lane,initial_coarse_index=acc_index,
                coarse_count=count,coarse_words=list(struct.unpack('<'+'I'*(count*2),coarse_bytes)),steps=steps)
            if map_size==128:
                record.update(source_bits=list(struct.unpack('<2I',struct.pack('<2f',*source))),
                    goal_bits=list(struct.unpack('<2I',struct.pack('<2f',*target))),
                    fine_count=fine_count,fine_index=actual_index,observed_obstruction=actual_obstruction,
                    fine_words=list(struct.unpack('<'+'I'*(fine_count*2),actual_fine)))
                long_cases.append(record)
            else:
                progress_cases.append(record)
        # Independently supply the selected intermediate/final destination to
        # the original fine request and compare full bytes, index and work.
        setup()
        machine.mem_write(target_ptr,struct.pack('<2f',*fine_goal))
        run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,700,radius_ptr,0)
        expected_count=read(route+0x1c)[0]
        expected_index=(expected_count-2 if expected_count>1 else 0) if read(system+0xd0)[0] else 0
        assert actual_fine==bytes(machine.mem_read(route_data,expected_count*8))
        assert actual_index==expected_index
        assert actual_pops==read(system+0x6c)[0]
    # Coarse object exclusion clears classifications across a rectangle, then
    # rebuilds from the fine map. Capture bytes at the actual search entry.
    exclusion=[]
    snapshot=[]
    def classes():
        return [machine.mem_read(data+cell*8+7,1)[0]
                for level,data in enumerate(storage)
                for cell in range((acc_side>>level)**2)]
    def observe_acc(uc,address,size,data):
        snapshot.append((classes(),read(obj+0x40)[0]))
    h=machine.hook_add(UC_HOOK_CODE,observe_acc,begin=0x6f162cb0,end=0x6f162cb0)
    machine.ctl_flush_tb()
    for rectangle,role,terrain in itertools.product(
            [(16,16,20,20),(17,17,21,21),(0,0,2,2)],['self','target','both'],[False,True]):
        width=height=64
        acc_side=32
        cls=0; acc_budget=400; lane=0; blocked=set()
        target=(59.25,59.75)
        setup_enabled()
        write(owner+0x23c,*maps)
        write(tilemap+0x54,0,0,height,width)
        write(obj+0x1c,*rectangle)
        write(obj+0x34,0x01000006,0)
        write(obj+0x40,0)
        write(links,0x01ffffff,obj)
        write(tilemap+0x88,1)
        y0,x0,y1,x1=rectangle
        for y in range(y0,y1):
            for x in range(x0,x1):
                write(cells+(y*width+x)*4,0xff000000 if terrain else 0)
        # Unrelated terrain just outside the half-open object rectangle but
        # inside its rounded-up coarse coverage must also be cleared/restored.
        write(cells+(y0*width+x1)*4,0xffffffff)
        # Initial and restored classifications come from original fine queries.
        run(0x6f15d360,owner,0,0)
        baseline=classes()
        # Expected base coverage uses floor(min/2)..floor(max/2), inclusive.
        for y in range(y0//2,y1//2+1):
            for x in range(x0//2,x1//2+1):
                address=storage[0]+(y*acc_side+x)*8+7
                machine.mem_write(address,b'\0')
        for level in range(1,4):
            side=acc_side>>level
            for y in range(side):
                for x in range(side):
                    address=storage[level]+(y*side+x)*8
                    machine.mem_write(address+7,b'\0')
                    for shift in [0,2,4,6]:
                        run(0x6f15d1c0,0,address,maps[level-1],shift,2*x,2*y)
        wanted=classes()
        guard=(y0//2)*acc_side+x1//2
        assert baseline[guard]!=0 and wanted[guard]==0
        run(0x6f15d360,owner,0,0)
        assert classes()==baseline
        write(path+0xa0,obj if role in ['self','both'] else 0,
              obj if role in ['target','both'] else 0)
        snapshot.clear()
        assert run(0x6f166c30,path,acc_source,acc_target,1)==1
        assert snapshot==[(wanted,0)],(rectangle,role,terrain)
        assert classes()==baseline,(rectangle,role,terrain)
        assert read(obj+0x40)[0]==0
        exclusion.append(dict(rectangle=rectangle,role=role,terrain=terrain,guard_cell=[x1,y0],
                              changed=sum(a!=b for a,b in zip(baseline,wanted))))
    machine.hook_del(h)
    # Formation destination -> original bounded accelerator query -> adjustment.
    formation=[]
    formation_group,formation_member,formation_mover=0x109c0000,0x109c0100,0x109c0200
    for name,cls,lane,offset in itertools.product(['open','wall','gap'],range(4),range(4),[0,2,18,20,22,40,42,48]):
        width=height=map_size=64
        acc_side=32
        acc_budget=400
        budget=700
        source=(8,8)
        target=(8+offset,8)
        blocked=set() if name=='open' else {(32,y) for y in range(64) if name=='wall' or not 29<=y<=34}
        setup_enabled()
        machine.mem_write(acc_source,struct.pack('<2f',4,4))
        machine.mem_write(acc_target,struct.pack('<2f',target[0]/2,target[1]/2))
        query=run(0x6f1627e0,acc,lane*2,acc_source,acc_target,30,cls//2,output,1) if offset else 0
        if name=='open': assert query==offset,(cls,lane,offset,query)
        query_point=struct.unpack('<2f',machine.mem_read(output,8))
        wanted=target if query<=20 else tuple(v*2 for v in query_point) if query==0xffffffff else source
        setup_enabled()
        write(owner+0x250,acc)
        machine.mem_write(formation_group,bytes(0x100))
        machine.mem_write(formation_member,bytes(0x40))
        machine.mem_write(formation_mover,bytes(0x100))
        machine.mem_write(formation_group+0x54,struct.pack('<2f',*source))
        machine.mem_write(formation_member+0xc,struct.pack('<2f',offset,0))
        write(formation_member+0x14,formation_mover)
        write(formation_member+0x28,0xffffffff)
        write(formation_mover+0xa8,path)
        run(0x6f16e250,formation_group,formation_member)
        actual=struct.unpack('<2f',machine.mem_read(formation_member+0x18,8))
        assert actual==wanted,(name,cls,lane,offset,query,query_point,actual,wanted)
        assert read(formation_member+0x28)[0]==(0xfff8ffff|(0x40000 if query>20 else 0))
        if engine:
            classes=[]
            for lev in range(4):
                side=acc_side>>lev
                classes.extend((machine.mem_read(storage[lev]+(y*side+x)*8+7,1)[0]>>(lane*2))&3 for y in range(side) for x in range(side))
            q=(ctypes.c_uint32*8)(acc_side,acc_side,cls//2,30,*struct.unpack('<4I',struct.pack('<4f',4,4,target[0]/2,target[1]/2)))
            out=(ctypes.c_uint32*3)(); data=(ctypes.c_uint8*len(classes))(*classes)
            engine.pathing_adaptive_distance(q,data,out)
            assert out[0]==query,(name,cls,lane,offset,'distance',out[0],query)
            point=struct.unpack('<2f',struct.pack('<2I',*out[1:]))
            endpoint=target if out[0]<=20 else tuple(v*2 for v in point) if out[0]==0xffffffff else source
            assert endpoint==actual,(name,cls,lane,offset,'endpoint',endpoint,actual)
        formation.append(dict(fixture=name,size_class=cls,lane=lane,offset=offset,query_result=query,destination=actual))
    if engine: engine_formation_cases=len(formation)
    intermediate_cases=sum(row['acc_index']>0 for row in enabled if row['map_size']!=128)
    assert intermediate_cases==96
    report=dict(binary_sha256=digest,scope=__doc__,passed=True,formation_destination_cases=len(formation),formation_destinations=formation,refill_cases=len(records),denied_refill_cases=denied_cases,cached_waypoint_cases=cached_cases,dynamic_refill_cases=len(dynamic),terrain_equivalence_cases=terrain_equivalence,hierarchy_exclusion_cases=len(exclusion),hierarchy_exclusion=exclusion,enabled_advance_cases=len(enabled),intermediate_waypoint_cases=intermediate_cases,enabled_advance=enabled,full_advance_cases=len(full_advance),full_advance=full_advance,dynamic_cases=dynamic,cases=records)
    if engine: report['engine_exact_formation_destination_cases']=engine_formation_cases
    if args.public_results_fixture or args.public_results_reference:
        results=[]; width=height=24
        for name,cls in itertools.product(['same_cell','blocked_start','blocked_goal','disconnected','insufficient_budget','special_target'],range(4)):
            source=(4.25,4.75); target=(19.25,19.75); budget=700
            if name=='same_cell':target=(4.625,4.875)
            blocked={(4,4)} if name=='blocked_start' else {(19,19)} if name=='blocked_goal' else {(12,y) for y in range(24)} if name=='disconnected' else set()
            if name=='insufficient_budget':budget=0
            setup()
            if name=='special_target':
                machine.mem_write(obj,bytes(0x80));write(obj+0x34,0x01000001,0);write(obj+0x40,0)
                write(links,0x01ffffff,obj);write(tilemap+0x88,1)
                for y in range(height):write(cells+(y*width+12)*4,0)
            machine.mem_write(path,bytes(0x100));machine.mem_write(path+0x1c,struct.pack('<4f',*target,*target))
            write(path+0x40,route_data);write(path+0x4c,1024,0);write(path+0x60,coarse);write(path+0x6c,1024,0,-1,-1)
            write(path+0x84,budget | 400<<16);write(path+0x9c,1 if name=='special_target' else 0x02000000)
            if name=='special_target':write(path+0xa4,obj)
            write(path+0xa8,-1,-1);machine.mem_write(path+0xb4,struct.pack('<f',.25+.5*cls))
            write(bucket,700 | 1<<16,1100,0,0,0,0,0);write(owner+0x538,100);write(owner,0,0)
            write(system+0xac+0xc,candidates);write(system+0xac+0x18,64,0)
            write(output,0xdeadbeef,0xdeadbeef)
            admitted=run(0x6f167ce0,path,source_ptr,output)
            n=read(path+0x50)[0];state=dict(count=n,index=read(path+0x74)[0],words=read(route_data,n*2),output=read(output,2),flags=read(path+0x88)[0],work=read(bucket+8)[0],timestamp=read(path+0x7c)[0],nodes=read(system+0x40)[0],obstruction=read(system+0xd0)[0])
            if engine:
                engine.pathing_fine_result_reset() # Matches setup's fresh native search object.
                words=[struct.unpack('<I',struct.pack('<f',v))[0]for v in (*source,*target)]
                objects=[12,0,13,24,0x01000001,1,1] if name=='special_target' else []
                grid=(ctypes.c_uint8*576)(*(2 if (x,y) in blocked else 0 for y in range(24) for x in range(24)))
                q=(ctypes.c_uint32*16)(24,24,4,4,int(target[0]),int(target[1]),budget,cls,1 if objects else 0x02000000,0,bool(objects),0 if objects else 0xffffffff,*words)
                out=(ctypes.c_uint32*(7+2*32768))()
                engine.pathing_fine_result_words(q,ctypes.byref(FineInput(grid,(ctypes.c_uint32*len(objects))(*objects) if objects else None)),out)
                assert list(out[1:5])==[state['work'],state['nodes'],n,state['index']],(name,cls,list(out[:7]),state)
                assert out[5]==state['obstruction'] and out[6]==bool(state['flags']&0x10000000),(name,cls,list(out[:7]),state)
                assert list(out[7:7+2*n])==state['words'],(name,cls,'route words')
            machine.mem_write(output,struct.pack('<2f',*target))
            advance=run(0x6f165ae0,path,source_ptr,output,mover)
            results.append(dict(name=name,cls=cls,source=list(source),target=list(target),budget=budget,admitted=admitted,refill=state,advance=advance,after=dict(count=read(path+0x50)[0],index=read(path+0x74)[0],words=read(route_data,read(path+0x50)[0]*2),output=read(output,2),retry=read(path+0x98)[0],flags=read(path+0x88)[0],work=read(bucket+8)[0]),target_suppression=read(obj+0x40)[0] if name=='special_target' else None))
        reused=[]
        for cls in range(4):
            source=(4.25,4.75);target=(19.25,19.75);budget=700;blocked={(19,19)}
            setup()
            result=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,budget,radius_ptr,0)
            assert result==0 and read(system+0xd0)[0]==1
            before=dict(stamp=read(system+0x20)[0]&65535,cls=read(system+0xa0)[0]&65535)
            target=(4.625,4.875);machine.mem_write(target_ptr,struct.pack('<2f',*target))
            # Deliberately different radius; setup returns before class initialization.
            machine.mem_write(radius_ptr,struct.pack('<f',.25+.5*((cls+1)%4)))
            result=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,0,radius_ptr,0)
            after=dict(stamp=read(system+0x20)[0]&65535,cls=read(system+0xa0)[0]&65535)
            record=dict(cls=cls,before=before,after=after,result=result,work=read(system+0x6c)[0],nodes=read(system+0x40)[0],obstruction=read(system+0xd0)[0],words=read(route_data,2))
            assert before==after and record['work']==record['nodes']==0 and record['obstruction']==1
            if engine:
                engine.pathing_fine_result_reset()
                grid=(ctypes.c_uint8*576)(*(2 if (x,y) in blocked else 0 for y in range(24)for x in range(24)))
                out=(ctypes.c_uint32*(7+2*32768))()
                for goal in [(19.25,19.75),target]:
                    words=[struct.unpack('<I',struct.pack('<f',v))[0]for v in (*source,*goal)]
                    q=(ctypes.c_uint32*16)(24,24,4,4,int(goal[0]),int(goal[1]),700 if goal[0]>5 else 0,cls,0x02000000,0,0,0xffffffff,*words)
                    engine.pathing_fine_result_words(q,ctypes.byref(FineInput(grid,None)),out)
                assert list(out[:6])==[1,0,0,1,0,1] and list(out[7:9])==record['words'],record
            reused.append(record)
        payload=dict(version=1,binary_sha256=digest,cases=results,same_cell_reuse=reused,scope='Unchanged167ce0 setup and165ae0 result consumption. Supplied24-cell terrain/object map and admitted owner bucket; four classes across same-cell, blocked source/goal, disconnected, zero-budget and suppressed special-target identity. No full public movement trajectory or outside-source claim.')
        if args.public_results_fixture:args.public_results_fixture.write_text(json.dumps(payload,indent=2)+'\n')
        if args.public_results_reference:assert payload==json.loads(args.public_results_reference.read_text())
        report['public_fine_result_cases']=len(results)
        report['same_cell_reuse_cases']=len(reused)
        if engine:report['engine_exact_public_fine_results']=len(results)
    if args.unit_budget_fixture or args.unit_budget_reference:
        unit_budget_cases=[]
        width=height=64; source=(4.25,4.75); target=(47.25,43.75)
        for pattern,cls,budget in itertools.product(range(2),range(4),[700,2048]):
            blocked={(x,y) for i,x in enumerate([12,22,32,42]) for y in range(64)
                if (y<54 if (i+pattern)%2==0 else y>9)}
            setup()
            result=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,budget,radius_ptr,0)
            n=read(route+0x1c)[0]; obstruction=read(system+0xd0)[0]
            words=read(route_data,n*2)
            record=dict(pattern=pattern,size_class=cls,budget=budget,result=result,fine_count=n,
                fine_index=n-2 if obstruction and n>1 else 0,observed_obstruction=obstruction,
                pops=read(system+0x6c)[0],nodes=read(system+0x40)[0],fine_words=words,
                source_bits=read(source_ptr,2),goal_bits=read(target_ptr,2))
            if engine:
                q=(ctypes.c_uint32*15)(64,64,4,4,47,43,budget,cls,0x02000000,0,0,*record['source_bits'],*record['goal_bits'])
                grid=(ctypes.c_uint8*(64*64))(*(2 if (x,y) in blocked else 0 for y in range(64) for x in range(64)))
                out=(ctypes.c_uint32*(6+2*16386))()
                engine.pathing_fine_request_words(q,ctypes.byref(FineInput(grid,None)),out)
                assert list(out[:6])==[result,record['pops'],record['nodes'],n,record['fine_index'],obstruction],record
                assert list(out[6:6+2*n])==words,record
            unit_budget_cases.append(record)
        payload=dict(version=1,binary_sha256=digest,source_entry='6f148100',cell_world=32,map_size=64,cases=unit_budget_cases,
            scope='Complete original fine requests on two alternating-wall maps, four classes,700 versus2048 attempts. Exact partial buffers, work, node creation, source and obstruction/initial-index policy. Clock/admission queues and adaptive group budgets excluded.')
        if args.unit_budget_reference:
            assert payload==json.loads(args.unit_budget_reference.read_text())
            report['unit_budget_reference_sha256']=hashlib.sha256(args.unit_budget_reference.read_bytes()).hexdigest()
        if args.unit_budget_fixture:
            args.unit_budget_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        report['unit_budget_cases']=len(unit_budget_cases)
        if engine:report['engine_exact_unit_budget_cases']=len(unit_budget_cases)
    if args.unit_route_fixture or args.unit_route_reference:
        unit_route_cases=[]
        width=height=64; acc_side=32; acc_budget=400; budget=700; lane=0
        source=(4.25,4.75); target=(47.25,43.75)
        for pattern,cls in itertools.product(range(2),range(4)):
            blocked={(x,y) for i,x in enumerate([12,22,32,42]) for y in range(64)
                if (y<54 if (i+pattern)%2==0 else y>9)}
            setup_enabled()
            result=run(0x6f165ae0,path,source_ptr,output,mover)
            count=read(path+0x70)[0]; n=read(path+0x50)[0]
            record=dict(pattern=pattern,size_class=cls,result=result,coarse_count=count,coarse_index=read(path+0x78)[0],
                coarse_words=read(coarse,count*2),coarse_work=read(acc+0x9c)[0],fine_count=n,fine_index=read(path+0x74)[0],
                fine_words=read(route_data,n*2),fine_work=read(system+0x6c)[0],output=read(output,2),
                source_bits=read(source_ptr,2),goal_bits=read(target_ptr,2),limits=read(path+0x84)[0])
            assert result==0 and count>1 and record['coarse_index']>0,record
            unit_route_cases.append(record)
        payload=dict(version=1,binary_sha256=digest,source_entry='6f165ae0',cell_world=32,map_size=64,cases=unit_route_cases,
            scope='Complete original enabled default400-accelerator/700-fine requests on two winding64-cell maps, four footprint classes. Engine48-cell gate is absent in the original default activation. Source/maps/counter/buckets are supplied; full group5000 budgets and owner physical motion excluded.')
        if args.unit_route_reference:
            assert payload==json.loads(args.unit_route_reference.read_text())
            report['unit_route_reference_sha256']=hashlib.sha256(args.unit_route_reference.read_bytes()).hexdigest()
        if args.unit_route_fixture:
            args.unit_route_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        report['unit_route_cases']=len(unit_route_cases)
    if args.adaptive_handoff_fixture or args.adaptive_handoff_reference:
        payload=dict(version=1,binary_sha256=digest,source_entry='6f165ae0',cell_world=32,map_size=64,cases=handoff_cases,
            scope='Complete original initial adaptive-to-fine request destination and both reconstructed route buffers on supplied static maps. Four classes/four lanes/open/solid-wall/gapped-wall. Owner cadence, dynamic blockers, fine-system index-init producer and retained multi-tick coarse progress excluded.')
        if args.adaptive_handoff_reference:
            frozen=json.loads(args.adaptive_handoff_reference.read_text())
            assert payload==frozen
            report['adaptive_handoff_exact_cases']=len(handoff_cases)
            report['adaptive_handoff_reference_sha256']=hashlib.sha256(args.adaptive_handoff_reference.read_bytes()).hexdigest()
        if args.adaptive_handoff_fixture:
            args.adaptive_handoff_fixture.parent.mkdir(parents=True,exist_ok=True)
            args.adaptive_handoff_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
    if args.adaptive_progress_fixture or args.adaptive_progress_reference:
        payload=dict(version=1,binary_sha256=digest,source_entry='6f165ae0',cell_world=32,map_size=64,cases=progress_cases,
            scope='Repeated complete original Path_Advance at supplied positions .48 accelerator units left of current coarse waypoints. Existing coarse buffer is unchanged; owner counter advances ten ticks and fine work budget resets are controlled admitted-boundary inputs, not a full owner scheduler. Open/gapped maps, four classes/four lanes. Physical motion, dynamic blockers, denied refills and clock cadence excluded.')
        if args.adaptive_progress_reference:
            assert payload==json.loads(args.adaptive_progress_reference.read_text())
            report['adaptive_progress_reference_sha256']=hashlib.sha256(args.adaptive_progress_reference.read_bytes()).hexdigest()
        if args.adaptive_progress_fixture:
            args.adaptive_progress_fixture.parent.mkdir(parents=True,exist_ok=True)
            args.adaptive_progress_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        report['adaptive_progress_cases']=len(progress_cases)
        report['adaptive_progress_steps']=sum(len(c['steps']) for c in progress_cases)
    if args.adaptive_long_fixture or args.adaptive_long_reference:
        payload=dict(version=1,binary_sha256=digest,source_entry='6f165ae0',cell_world=32,map_size=128,cases=long_cases,
            scope='Complete original initial and repeated coarse/fine requests on 128-cell static maps. Controlled native sources at .48 accelerator units from each retained waypoint; ten owner ticks between admitted requests. Four classes/four lanes, open/gapped wall. Physical motion, dynamic blockers and denied scheduling excluded.')
        if args.adaptive_long_reference:
            assert payload==json.loads(args.adaptive_long_reference.read_text())
            report['adaptive_long_reference_sha256']=hashlib.sha256(args.adaptive_long_reference.read_bytes()).hexdigest()
        if args.adaptive_long_fixture:
            args.adaptive_long_fixture.parent.mkdir(parents=True,exist_ok=True)
            args.adaptive_long_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        report['adaptive_long_cases']=len(long_cases)
        report['adaptive_long_steps']=sum(len(c['steps']) for c in long_cases)
    if cached_route_cases:report['cached_route_cases']=len(cached_route_cases)
    if engine:report['engine_exact_obstruction_cases']=engine_obstruction_cases
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['formation_destinations','cases','dynamic_cases','full_advance','enabled_advance','hierarchy_exclusion']},indent=2))


if __name__=='__main__':
    main()
