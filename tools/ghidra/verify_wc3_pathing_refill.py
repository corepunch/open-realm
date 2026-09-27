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


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
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
    for map_size,name,cls,acc_budget,lane in itertools.product([24,64],['open','wall','gap'],range(4),[0,5,400],range(4)):
        width=height=map_size
        target=(map_size-4.75,map_size-4.25)
        blocked=set() if name=='open' else {(width//2,y) for y in range(height)
                  if name=='wall' or not height//2-3<=y<=height//2+2}
        acc_side=16 if map_size==24 else 32
        budget=700
        def setup_enabled():
            setup()
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
        fine_goal=target if acc_index==0 else tuple(v*2 for v in struct.unpack('<2f',coarse_bytes[acc_index*8:acc_index*8+8]))
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
        formation.append(dict(fixture=name,size_class=cls,lane=lane,offset=offset,query_result=query,destination=actual))
    intermediate_cases=sum(row['acc_index']>0 for row in enabled)
    assert intermediate_cases==96
    report=dict(binary_sha256=digest,scope=__doc__,passed=True,formation_destination_cases=len(formation),formation_destinations=formation,refill_cases=len(records),denied_refill_cases=denied_cases,cached_waypoint_cases=cached_cases,dynamic_refill_cases=len(dynamic),terrain_equivalence_cases=terrain_equivalence,hierarchy_exclusion_cases=len(exclusion),hierarchy_exclusion=exclusion,enabled_advance_cases=len(enabled),intermediate_waypoint_cases=intermediate_cases,enabled_advance=enabled,full_advance_cases=len(full_advance),full_advance=full_advance,dynamic_cases=dynamic,cases=records)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['formation_destinations','cases','dynamic_cases','full_advance','enabled_advance','hierarchy_exclusion']},indent=2))


if __name__=='__main__':
    main()
