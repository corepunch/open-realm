#!/usr/bin/env python3
"""Original point-order production and complete task-chain dispatch through movement acceptance.

Runs unmodified retail code with real vtables and preallocated allocator blocks.
No retail bytes, code patches, replacement callbacks or external allocator stubs.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import add as float_add, multiply as float_multiply


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_INVALID, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
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
    def invalid_memory(uc,access,address,size,value,data):
        print(f'retail memory access {access} at {address:#x}, size {size}, EIP {uc.reg_read(UC_X86_REG_EIP):#x}',flush=True)
        return False
    machine.hook_add(UC_HOOK_MEM_INVALID,invalid_memory)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])

    machine.mem_map(0x10000000, 0x40000)
    machine.mem_map(0x20000000, 0x10000)
    system = 0x10000000
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments, edx=0):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.reg_write(UC_X86_REG_EDX, edx)
        preserved={r:0x10203040+i*0x111111 for i,r in enumerate((UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP))}
        for r,v in preserved.items():machine.reg_write(r,v)
        machine.emu_start(entry, stop, count=100000)
        assert machine.reg_read(UC_X86_REG_ESP)==stack+4+4*len(arguments),(hex(entry),'stack')
        assert all(machine.reg_read(r)==v for r,v in preserved.items()),(hex(entry),'callee saved registers')
        assert read(0)[0]==0,(hex(entry),'exception chain')
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail lifetime call exceeded instruction budget')

    machine.mem_map(0, 0x1000)  # Original FS:[0] exception chain.
    host, owner, registry, slots, wrapper, payload, pool, request_block, heap, bucket, entry, hash_input = [
        system+n for n in (0,0x1000,0x2000,0x2100,0x3004,0x4004,0x5000,0x6000,0x6100,0x7000,0x7100,0x7200)]
    unit, ability, inputs, message = [system+n for n in (0xa000,0xa800,0xb000,0xb100)]
    write(0x6fd3c82c,host)
    write(0x6fd53a48,owner)
    write(0x6fd68610,registry)
    write(0x6fd3c744,0)
    write(owner+0x10,0x6f04c220)
    write(owner+0x254,0x6f04d9c0)
    write(host+0x28,bucket,0,0)
    write(bucket,8,0,0)
    classes=[
      ('point',0x74736b2e,0x6fd70f1c,0x6fb78eb0,0x50),
      ('event',0x7461736b,0x6fd70ea4,0x6fb78bcc,0x34),
      ('action',0x74736b41,0x6fd70eec,0x6fb78d88,0x38),
      ('orderparam',0x74736b4f,0x6fd70f04,0x6fb78e1c,0x38),
      ('ordertarget',0x6f726474,0x6fd70e44,0x6fb78984,0x88),
    ]
    for n,(name,rawcode,factory,vt,psize) in enumerate(classes):
        write(factory,vt)
        run(0x6f06a270,factory+4,psize,32)
        block=system+0x10000+n*0x1800
        write(factory+0x14,block)
        for i in range(32):write(block+i*0xc0,block+(i+1)*0xc0 if i<31 else 0)
        row=system+0x8000+n*0x100
        write(hash_input,rawcode)
        run(0x6f198420,hash_input)
        write(row+4,machine.reg_read(UC_X86_REG_EAX))
        write(row+0xc,read(bucket+8)[0])
        write(row+0x18,rawcode)
        write(row+0x6c,row+0x80,factory)
        write(bucket+8,row)
    write(host+0x34,pool)
    write(pool+0x14,system+0x30000)
    for i in range(64):
        w=system+0x30000+i*0x100
        write(w,w+0x100 if i<63 else 0)
        run(0x6f056f20,w+4)
        write(w+4,0x6fa8099c)
    write(registry+0xc,slots)
    write(registry+0x18,128,128)
    write(registry+0x40,0)
    write(registry+0x50,100)
    for i in range(128):write(slots+i*8,i+1 if i<127 else -1,0)
    write(unit,0x6fb77eb0)
    write(unit+0x174,-1,-1)
    # Snapshot preallocated authentic pools. Only original code allocates payloads/wrappers.
    initial_world=bytes(machine.mem_read(system,0x40000))
    initial_image=bytes(machine.mem_read(base,size))
    dirty_pages=set()
    def wrote(uc,access,address,length,value,data):
        if base<=address<base+size:
            dirty_pages.update(range(address&~4095,(address+length+4095)&~4095,4096))
    machine.hook_add(UC_HOOK_MEM_WRITE,wrote)
    def reset():
        machine.mem_write(system,initial_world)
        for page in dirty_pages:machine.mem_write(page,initial_image[page-base:page-base+4096])
        dirty_pages.clear()
    def create_order(x,y,code=0xd0012):
        run(0x6f055d00,inputs+0x40,0x6fd70e44,edx=0x6f726474)
        run(0x6f154b60,inputs+0x40,1,edx=1)
        wr=machine.reg_read(UC_X86_REG_EAX)
        obj=read(wr+0x54)[0]
        assert read(obj)[0]==0x6fb78994
        assert read(obj+4)[0]==1
        assert read(obj+0x58,2)==[0xffffffff,0xffffffff]
        write(obj+0x24,code)
        write(inputs,x,y)
        run(0x6f247520,obj+0x44,inputs,1)
        run(0x6f247520,obj+0x4c,inputs+4,1)
        return obj
    def chain():
        identity=read(unit+0x174,2)
        result=[]
        while identity[0]!=0xffffffff:
            assert len(result)<64
            assert read(slots+identity[0]*8)[0]==0xfffffffe
            wr=read(slots+identity[0]*8+4)[0]
            assert read(wr+0x14,2)==identity
            obj=read(wr+0x54)[0]
            assert read(obj+0xc,2)==identity
            assert read(obj+4)[0]>=1
            result.append((obj,identity,read(obj+0x30)[0]))
            identity=read(obj+0x24,2)
        assert identity==[0xffffffff,0xffffffff]
        return result
    direct_cases=0
    values=[0,0x80000000,0x3f800000,0x3f800001,0xbf800001,0x3a83126f,
            0x44800000,0xc4800000,0x477fff00,0xc77fff00,1,0x807fffff]
    import random
    rng=random.Random(0x692120)
    triples=[(values[i%len(values)],values[(i*5+1)%len(values)],values[(i*7+2)%len(values)]) for i in range(36)]
    triples += [tuple(rng.randrange(0x3a000000,0x47800000)|(rng.randrange(2)<<31) for _ in range(3)) for i in range(128)]
    for code in (0xd016b,0xd016c,0xd016e):
        for case,(x,y,radius) in enumerate(triples):
            reset()
            retained=create_order(0,0) if case%2 else 0
            write(inputs,x,y,radius)
            before=bytes(machine.mem_read(inputs,12))
            run(0x6f692120,unit,code,inputs,inputs+4,inputs+8,retained)
            tasks=chain();assert len(tasks)==1
            obj,identity,actual_code=tasks[0]
            assert actual_code==code and read(obj)[0]==0x6fb78ec0
            assert read(obj+0x34,7)==[0x6fa9de14,x,0x6fa9de14,y,0x6fa9de14,radius,retained]
            assert bytes(machine.mem_read(inputs,12))==before
            if retained:assert read(retained+4)[0]==2
            # A second call proves real allocation and prepend, not just fresh-head publication.
            run(0x6f692120,unit,code,inputs+4,inputs,inputs+8,retained)
            newer=chain();assert len(newer)==2 and newer[1]==tasks[0]
            assert newer[0][1][0]==identity[0]+1 and newer[0][1][1]==identity[1]+1
            assert read(newer[0][0]+0x38)[0]==y and read(newer[0][0]+0x40)[0]==x
            if retained:assert read(retained+4)[0]==3
            direct_cases+=2
    full_cases=[]
    coords=[(0,0),(0x80000000,0),(0x3f800001,0xbf800001),(0x43a01234,0xc2345678),
            (0x44800000,0xc4800000),(0x477fff00,0xc77fff00)]
    creation_base=[0xd0162,0xd0166,0xd0148,0xd014a]
    for incoming_code in (0xd0003,0xd0012,0xd0016):
     for flag in (0,1):
       for player in (0,11,12,15):
        for oldhead in (False,True):
         for x,y in coords:
             reset()
             if oldhead:
                 write(inputs,0x3f800001,0xc0123456,0x3f000000)
                 run(0x6f692120,unit,0xd016c,inputs,inputs+4,inputs+8,0)
             previous=chain()
             order=create_order(x,y,incoming_code)
             order_before=bytes(machine.mem_read(order,0x88))
             write(ability,0x6fb62794)
             write(ability+0x20,0x100)
             write(ability+0x30,unit)
             write(unit+0x58,player)
             write(message,0,0,incoming_code,order)
             run(0x6f5fd270,ability,message,flag,0)
             creation=creation_base+([] if flag else [0xd0165])+[0xd016b,0xd014e]
             if not flag:
                 if player>11:creation+=[0xd014f]
                 creation+=[0xd014c,0xd0165]
             else:creation+=[0xd0148]
             creation+=[0xd0178,0xd0144,0xd0162]
             tasks=chain();produced=tasks[:len(creation)]
             assert [t[2] for t in produced]==creation[::-1]
             assert tasks[len(creation):]==previous
             assert bytes(machine.mem_read(order,0x88))==order_before
             assert read(ability+0x20)[0]==0x80
             point=[p for p,i,c in produced if c==0xd016b][0]
             assert read(point+0x34,7)==[0x6fa9de14,x,0x6fa9de14,y,0x6fa9de14,0,0]
             assert read(produced[0][0]+0x34)[0]==7
             assert read(produced[-1][0]+0x34)[0]==0
             orderparam=[p for p,i,c in produced if c==0xd0166][0]
             assert read(orderparam+0x34)[0]==0xd0012
             ids=[i for p,i,c in produced]
             assert all(a[0]==b[0]+1 and a[1]==b[1]+1 for a,b in zip(ids,ids[1:]))
             assert all(read(p+4)[0]==1 for p,i,c in produced)
             full_cases.append(dict(incoming_code=incoming_code,flag=flag,player=player,previous_head=oldhead,x=x,y=y,
                                    creation_codes=creation,point_identity=read(point+0xc,2)))
    # Load the shipped CRT's real transform math; relocate data, never replace code.
    crt=(args.binary.parent/'msvcr120.dll').read_bytes()
    crt_digest=hashlib.sha256(crt).hexdigest()
    if crt_digest!='86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f':
        parser.error('unsupported shipped msvcr120.dll')
    cp=struct.unpack_from('<I',crt,0x3c)[0]; co=cp+24
    old_base,crt_size=struct.unpack_from('<I',crt,co+28)[0],struct.unpack_from('<I',crt,co+56)[0]
    crt_base=0x50000000
    machine.mem_map(crt_base,(crt_size+4095)&~4095)
    machine.mem_write(crt_base,crt[:struct.unpack_from('<I',crt,co+60)[0]])
    for i in range(struct.unpack_from('<H',crt,cp+6)[0]):
        section=co+struct.unpack_from('<H',crt,cp+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',crt,section+12)
        if count: machine.mem_write(crt_base+va,crt[offset:offset+count])
    reloc,reloc_size=struct.unpack_from('<II',crt,co+96+5*8)
    cursor=crt_base+reloc
    while cursor<crt_base+reloc+reloc_size:
        page,block=read(cursor,2)
        if not block:break
        for item in struct.unpack('<'+'H'*((block-8)//2),machine.mem_read(cursor+8,block-8)):
            if item>>12==3:
                address=crt_base+page+(item&4095)
                write(address,read(address)[0]+crt_base-old_base)
            else:assert item>>12==0
        cursor+=block
    export=crt_base+struct.unpack_from('<I',crt,co+96)[0]
    count,functions,names,ordinals=read(export+24,4)
    imports={'_libm_sse2_sin_precise':0x6fa7c48c,'_libm_sse2_cos_precise':0x6fa7c47c,'_libm_sse2_sqrt_precise':0x6fa7c4f8,'_CIatan2':0x6fa7c450,'_libm_sse2_asin_precise':0x6fa7c44c}
    resolved_crt_exports={}
    for i in range(count):
        name_address=crt_base+read(crt_base+names+i*4)[0]
        name=bytes(machine.mem_read(name_address,100)).split(b'\0',1)[0].decode()
        if name in imports:
            ordinal=struct.unpack('<H',machine.mem_read(crt_base+ordinals+i*2,2))[0]
            entry=crt_base+read(crt_base+functions+ordinal*4)[0]
            write(imports[name],entry)
            resolved_crt_exports[name]=dict(iat=hex(imports[name]),entry=hex(entry))
    assert resolved_crt_exports.keys()==imports.keys()

    machine.mem_map(0x10100000,0x30000)
    machine.mem_map(0x10200000,0x100000)
    def dispatch_case(flag,player,status,target,next_target=None,replacement_target=None,replacement_mode=1):
        reset()
        machine.mem_write(0x10100000,bytes(0x30000))
        machine.mem_write(0x10200000,bytes(0x100000))
        for exp in resolved_crt_exports.values():write(int(exp["iat"],16),int(exp["entry"],16))
        extra=0x10100000
        write(host+8,1)
        def floats(address,*values):
            machine.mem_write(address,struct.pack('<'+'f'*len(values),*values))
        mover,unit_wrapper,subtable,subbuckets,subnodes=[extra+n for n in (0,0x100,0x200,0x300,0x400)]
        release_nodes,release_heap=extra+0x1000,extra+0x2000
        terrain,vertices,bridges=extra+0x3000,extra+0x6000,extra+0x7000
        maps=[extra+0x8000,extra+0x8200]
        objects=[extra+0x8400,extra+0x8500]
        current_path=extra+0x8600
        fine,acc,accmap=extra+0x9000,extra+0xa000,extra+0xb000
        generator,group,path,members=[extra+n for n in (0xc004,0xd004,0xe004,0xf000)]
        profile,profilebucket=extra+0x11000,extra+0x11300
        write(slots+125*8,-1,0)
        write(slots+126*8,-2,unit_wrapper,-2,mover)
        write(registry+0x48,3)  # Existing unit, mover and attached ability identities.
        write(unit_wrapper,0x6fa8099c)
        write(unit_wrapper+0xc,0x2b61676c,0x2b616761,126,900)
        write(unit_wrapper+0x54,unit)
        write(unit+4,1,subtable,126,900)
        write(unit+0x19c,-1,-1)
        write(unit+0x1a8,-1,-1)
        write(unit+0x1dc,-1,-1)
        write(unit+0x240,-1,-1)
        write(unit+0x30,0x68666f6f)
        write(unit+0x58,player,status)
        write(unit+0x164,0x6fac2f10,0,127,901)
        floats(unit+0x284,128,128,0,0)
        write(mover,0x6fa9129c,owner+0x200,0)
        write(mover+0x14,127,901)
        write(mover+0x30,unit_wrapper)
        floats(mover+0x78,4,4,0,0,8,0,0.25)
        write(mover+0x94,*objects)
        write(mover+0x9c,-1,-1)
        write(mover+0xa8,current_path)
        floats(mover+0xc8,0.25)
        write(mover+0xd0,4,4)
        write(ability,0x6fb62794,1)
        # Attached Move traverses the actual ability list; its virtual184 returns zero.
        ability_wrapper=extra+0x1d000
        write(slots+123*8,-1,0,-2,ability_wrapper)
        write(ability_wrapper,0x6fa8099c)
        write(ability_wrapper+0xc,0x2b61676c,0x2b616761,124,902)
        write(ability_wrapper+0x54,ability)
        write(ability+0xc,124,902)
        write(ability+0x24,-1,-1)
        write(unit+0x1dc,124,902)
        classrow,cachebucket,cacheentry=extra+0x1d100,extra+0x1d200,extra+0x1d240
        write(inputs,0x416d6f76)
        run(0x6f198420,inputs)
        write(classrow+4,machine.reg_read(UC_X86_REG_EAX))
        write(classrow+0xc,read(bucket+8)[0])
        write(classrow+0x18,0x416d6f76)
        write(bucket+8,classrow)
        write(classrow+0x38,cachebucket)
        write(classrow+0x40,0)
        write(cachebucket,0,0,cacheentry)
        for n,query in enumerate((0x42706c79,0x4255736c,0x41736c61)):
            row=cacheentry+n*0x20
            write(inputs,query)
            run(0x6f198420,inputs)
            write(row,machine.reg_read(UC_X86_REG_EAX),row+0x20 if n<2 else 0)
            write(row+0x14,query,0)
        write(ability+0x30,unit)
        write(ability+0x84,-1,-1)
        write(ability+0xcc,-1,-1)
        write(ability+0xd8,-1,-1)
        floats(ability+0x70,256)
        floats(ability+0x78,1)
        write(subtable,64<<8,subbuckets)
        run(0x6f06a270,0x6fd3cce4,0x10,64)
        write(0x6fd3ccf4,subnodes)
        for i in range(64):write(subnodes+i*16,subnodes+(i+1)*16 if i<63 else 0)
        # Unit self subscriptions are installed by retail68a360..68a38f.
        for event in (0xd0148,0xd014c,0xd0162):run(0x6f0725b0,unit,event,event,unit)
        run(0x6f5fe6a0,ability,1)  # All twenty Move task subscriptions, original registration procedure.
        run(0x6f0725b0,unit,0xd0166,0xd0166,ability)  # Additional subscription installed by5fcd80.
        clock=owner+0x14
        write(clock+0x10,release_heap)
        write(clock+0x1c,64,1)
        write(clock+0x38,release_nodes)
        for i in range(32):write(release_nodes+i*0x40,release_nodes+(i+1)*0x40 if i<31 else 0)
        write(0x6fd726c0,terrain)
        write(0x6fd726cc,bridges)
        write(terrain,1)
        write(terrain+0xb4,4,4,16,16)
        write(terrain+0xe0,25,vertices)
        for y in range(5):
            for x in range(5):write(vertices+(y*5+x)*0x1c,0x80402000+x*0x800000+y*0x4000,y*5+x)
        for n,(grid,obj) in enumerate(zip(maps,objects)):
            data,links,bitmap=extra+0x12000+n*0x4000,extra+0x13000+n*0x4000,extra+0x15000+n*0x4000
            write(grid+0x28,data)
            write(grid+0x3c,16,16)
            write(grid+0x54,0,0,16,16)
            floats(grid+0x68,1)
            write(grid+0x78,links)
            write(grid+0x84,1024,0)
            write(grid+0x98,bitmap)
            write(grid+0xac,0xffffff)
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
            write(obj+0x2c,grid)
            write(obj+0x34,0x01000001)
        write(owner+0x24c,fine,acc)
        write(fine+0x1c,maps[1])
        write(acc+0x1c,accmap)
        write(accmap+0x54,0,0,16,16)
        for ptr,ctor,offset in [(generator,0x6f169220,0x638),(group,0,0x678),(path,0x6f1657c0,0x958)]:
            if ctor:run(ctor,ptr)
            else:
                write(ptr,0x6fa90d64)
                write(ptr+0x14,-1,-1)
                write(ptr+0x1c,0x6fa90d5c,members,12*0x2c,members,12*0x2c,0,12,0)
                write(ptr+0x40,-1,-1)
            write(owner+offset+0x14,ptr-4,0,0)
        write(group+0x28,members)
        write(group+0x34,12)
        write(profilebucket,0,0,profile)
        write(profile+0x14,0x68666f6f)
        run(0x6f198420,profile+0x14)
        write(profile,machine.reg_read(UC_X86_REG_EAX))
        floats(profile+0x1d8,1,522)
        write(0x6fd709f4,profilebucket)
        write(0x6fd709fc,0)
        floats(host+0x6c,0,0,512,512,0,522)
        write(0x6fd687a8,extra+0x1a000)
        write(extra+0x1a3e0,1)
        for entry in [0x6f016290,0x6f0162b0,0x6f0162c0,0x6f0162d0,0x6f016210,0x6f016220,0x6f016230,0x6f016240]:run(entry,0)
        order_class_row=system+0x8400
        order_cache_bucket,order_cache_entry=extra+0x1e000,extra+0x1e040
        write(order_class_row+0x38,order_cache_bucket)
        write(order_class_row+0x40,0)
        write(order_cache_bucket,0,0,order_cache_entry)
        write(inputs,0x2b6f7264)
        run(0x6f198420,inputs)
        write(order_cache_entry,machine.reg_read(UC_X86_REG_EAX),0)
        write(order_cache_entry+0x14,0x2b6f7264,1)  # COrderTarget derives COrder.
        nodes,search_heap,member_route,group_route,member_coarse=[0x10200000+n for n in (0,0x30000,0x60000,0x64000,0x68000)]
        write(fine+0x1c,maps[1],1)
        write(fine+0x30,nodes)
        write(fine+0x3c,4096,0)
        write(fine+0x50,search_heap)
        write(fine+0x5c,32768,1,-3,100000,0)
        for ptr,route in [(current_path,member_route),(path,group_route)]:
            write(ptr+0x40,route)
            write(ptr+0x4c,1024,0)
            write(ptr+0x60,member_coarse if ptr==current_path else group_route+0x2000)
            write(ptr+0x6c,1024,0)
        write(current_path+0x84,700|(400<<16))
        write(current_path+0x9c,0x02000000)
        for row in range(16):
            for kind,(limit,reload,budget) in enumerate([(5000,3,800),(2000,2,300),(400,2,900),(700,1,1100)]):
                write(0x6fd53a90+row*0x70+kind*0x1c,limit|(reload<<16),budget,0,0,0,0,0)
        write(owner+0x538,100)
        floats(inputs,0.5)
        run(0x6f05c8c0,unit+0x164,inputs)
        run(0x6f05c890,unit+0x164,inputs)
        order=create_order(*target)
        initial_command=0xd0014 if flag else 0xd0012
        write(order+0x24,initial_command)
        original_order=bytes(machine.mem_read(order,0x88))
        order_identity=read(order+0xc,2)
        order_wrapper=read(slots+order_identity[0]*8+4)[0]
        dispatched=[]
        def observe_dispatch(uc,address,length,data):
            packet=read(uc.reg_read(UC_X86_REG_ESP)+4)[0]
            dispatched.append((hex(address),hex(read(packet+8)[0])))
        admissions=[];arrivals=[]
        def observe_progress(uc,address,length,data):
            now=read(clock+0x40)[0]
            head=read(unit+0x19c,2)
            if address==0x6f5fd270:
                packet=read(uc.reg_read(UC_X86_REG_ESP)+4)[0]
                incoming=read(packet+0xc)[0]
                assert read(unit+0x174,2)==[0xffffffff]*2
                admissions.append(dict(clock_bits=now,order_identity=read(incoming+0xc,2),
                                       target_bits=[read(incoming+0x48)[0],read(incoming+0x50)[0]],
                                       user_head=head,queue_count=read(unit+0x1b4)[0]))
            else:
                task=chain()[0][0]
                arrivals.append(dict(clock_bits=now,user_head=head,
                                     target_bits=[read(task+0x38)[0],read(task+0x40)[0]]))
        progress_hooks=[machine.hook_add(UC_HOOK_CODE,observe_progress,begin=a,end=a) for a in (0x6f5fd270,0x6f5fa7a0)]
        dispatch_hooks=[machine.hook_add(UC_HOOK_CODE,observe_dispatch,begin=a,end=a) for a in (0x6f071da0,0x6f5fda10,0x6f690490)]
        ui_bucket,ui_row=extra+0x1b000,extra+0x1b100
        write(0x6fd6a698,ui_bucket)
        write(0x6fd6a6a0,0)
        write(ui_bucket,0,0,ui_row)
        write(inputs,0x68666f6f)
        run(0x6f198420,inputs)
        write(ui_row+4,machine.reg_read(UC_X86_REG_EAX))
        write(ui_row+0x18,0x68666f6f)
        pi=struct.unpack('<f',machine.mem_read(0x6fa8af08,4))[0]
        angle=struct.unpack('<I',struct.pack('<f',10*pi/180))[0]
        run(0x6f352db0,0x68666f6f,angle)
        run(0x6f352dd0,0x68666f6f,angle)
        run(0x6f352cf0,0x68666f6f,0x41a00000)
        layer,samples=extra+0x1b800,extra+0x1c000
        write(terrain+0x79c,layer)
        write(layer,16,16)
        floats(layer+8,32,32)
        write(layer+0x18,samples)
        assert read(unit+4)[0]==4
        initial_dispatch=[]
        def capture_initial_chain(uc,address,length,data):
            initial_dispatch.append(dict(chain=chain(),prelude=list(dispatched)))
            dispatched.clear()
        initial_hook=machine.hook_add(UC_HOOK_CODE,capture_initial_chain,begin=0x6f67df00,end=0x6f67df00)
        machine.ctl_flush_tb()
        run(0x6f680320,unit,order,1,1)
        machine.hook_del(initial_hook)
        assert len(initial_dispatch)==1
        assert read(unit+4)[0]==4
        assert initial_dispatch[0]['prelude']==[('0x6f071da0','0xd0144'),('0x6f5fda10','0xd0144'),('0x6f071da0','0xd0162'),('0x6f690490','0xd0162'),('0x6f5fda10',hex(initial_command))]
        before_chain=initial_dispatch[0]['chain']
        # d0014 enters flag1, omits the early d0165, then the zero value
        # returned by058040 normalizes the final policy to flag0.
        expected_creation=creation_base+([] if flag else [0xd0165])+[0xd016b,0xd014e]
        if player>11:expected_creation+=[0xd014f]
        expected_creation += [0xd014c,0xd0165,0xd0178,0xd0144,0xd0162]
        assert [t[2] for t in before_chain]==expected_creation[::-1]
        assert read([p for p,i,c in before_chain if c==0xd0166][0]+0x34)[0]==0xd0012
        initial_admission=admissions.pop(0)
        assert not admissions
        assert initial_admission['order_identity']==order_identity
        assert initial_admission['queue_count']==1
        assert read(unit+0x19c,2)==order_identity
        assert read(unit+0x1b4)[0]==1
        after_chain=chain()
        point_index=next(n for n,t in enumerate(before_chain) if t[2]==0xd016b)
        assert after_chain==before_chain[point_index:]
        task_events=[int(code,16) for address,code in dispatched if address=='0x6f071da0']
        assert task_events[:point_index+1]==[t[2] for t in before_chain[:point_index+1]]
        assert all(read(p+4)[0]==1 for p,i,c in before_chain)
        assert read(order+0x24)[0]==initial_command
        assert [read(order+0x48)[0],read(order+0x50)[0]]==list(target)
        assert read(clock+0x3c)[0]==read(clock+0x50)[0]==point_index+1
        assert read(unit+0x194)[0]==7
        assert read(unit+0x5c)[0]==status
        assert read(ability+0x20)[0]==(0x884 if player<12 else 0x84)
        assert read(mover+0x9c,2)==read(group+0x14,2)
        assert read(group+0x38,2)==[1,path]
        assert read(members,2)==[127,901] and read(members+0x14)[0]==mover
        target_grid=[((w&0x7fffffff)-0x02800000)|(w&0x80000000) for w in target]
        assert read(group+0x4c,4)==target_grid+[0x40800000,0x40800000]
        assert read(path+0x1c,6)==target_grid*3
        assert read(mover+0x88)[0]==0x41000000
        assert read(path+0x84,4)==[(5000<<16)|700,0x400000,0,0]
        assert read(owner+0x3b8)[0]==group
        assert read(generator+0x14,2)==[0xffffffff]*2
        assert read(owner+0x638+0x14,2)==[generator-4,0]
        assert read(owner+0x678+0x14,2)==[0,1]
        assert read(owner+0x958+0x14,2)==[0,1]
        accepted=dict(flag=flag,player=player,status=status,target_bits=target,
                initial_command=hex(initial_command),initial_admission=initial_admission,
                initial_admission_prelude=initial_dispatch[0]['prelude'],
                popped_tasks=point_index,remaining_codes=[t[2] for t in after_chain],
                dispatch=list(dispatched),group_identity=read(group+0x14,2),path_identity=read(path+0x14,2))
        second_order=0
        queue_transitions=[]
        expected_task_codes=[t[2] for t in before_chain]
        group_objects=[group];path_objects=[path]
        if next_target:
            second_group,second_path,second_members=extra+0x23004,extra+0x24004,extra+0x25000
            write(second_group,0x6fa90d64)
            write(second_group+0x14,-1,-1)
            write(second_group+0x1c,0x6fa90d5c,second_members,12*0x2c,second_members,12*0x2c,0,12,0)
            write(second_group+0x40,-1,-1)
            run(0x6f1657c0,second_path)
            write(owner+0x678+0x14,second_group-4)
            write(owner+0x958+0x14,second_path-4)
            group_objects.append(second_group);path_objects.append(second_path)
            second_order=create_order(*next_target)
            second_identity=read(second_order+0xc,2)
            second_wrapper=read(slots+second_identity[0]*8+4)[0]
            first_chain=chain()
            run(0x6f693490,unit,second_order)
            assert chain()==first_chain
            assert read(unit+0x19c,2)==order_identity
            assert read(unit+0x1a8,2)==second_identity
            assert read(order+0x2c,2)==second_identity
            assert read(unit+0x1b4)[0]==2
            expected_task_codes += [0xd0162,0xd0144,0xd0178,0xd0165,0xd014c,0xd014e,
                                    0xd016b,0xd0165,0xd014a,0xd0148,0xd0166,0xd0162]
            write(second_path+0x40,0x10270000)
            write(second_path+0x4c,1024,0)
            write(second_path+0x60,0x10272000)
            write(second_path+0x6c,1024,0)
        run(0x6f16c150,group)
        initial_route=read(member_route,read(current_path+0x50)[0]*2)
        assert len(initial_route)>=4
        assert initial_route[:2]==target_grid and initial_route[-2:]==[0x40800000]*2
        write(owner+0x210,extra+0x21000)
        write(owner+0x230,0)
        floats(clock+0x48,8)
        floats(inputs,1/32)
        trajectory=[]
        replacement_evidence=None
        machine.ctl_flush_tb()
        for tick in range(1,129):
            run(0x6f054190,inputs,edx=clock)
            oldpos=read(mover+0x78,2);oldvel=read(mover+0x80,2)
            run(0x6f16c150,group)
            want=[float_add(p,float_multiply(v,0x3d000000)) for p,v in zip(oldpos,oldvel)]
            assert read(mover+0x78,2)==want,(tick,read(mover+0x78,2),want)
            trajectory.append(dict(tick=tick,position=want,velocity=read(mover+0x80,2)))
            if replacement_target and tick==3:
                assert not arrivals and not admissions
                abandoned_identity=list(second_identity)
                replacement_order=create_order(*replacement_target)
                replacement_identity=read(replacement_order+0xc,2)
                replacement_wrapper=read(slots+replacement_identity[0]*8+4)[0]
                before_refs=read(unit+4)[0]
                run(0x6f680320,unit,replacement_order,replacement_mode,1)
                if not replacement_mode:
                    assert read(unit+0x19c,2)==replacement_identity
                    assert read(unit+0x1a8,2)==abandoned_identity
                    assert read(unit+0x1b4)[0]==3
                    assert read(replacement_order+0x2c,2)==order_identity
                    assert read(order+0x2c,2)==abandoned_identity
                    assert not arrivals and len(admissions)==1
                else:
                    assert read(unit+0x19c,2)==replacement_identity
                    assert read(unit+0x1a8,2)==replacement_identity
                    assert read(unit+0x1b4)[0]==1
                assert read(mover+0x9c,2)==read(second_group+0x14,2)
                replacement_evidence=dict(tick=tick,abandoned_queued_identity=abandoned_identity,
                    replacement_identity=replacement_identity,target_bits=replacement_target,
                    unit_refs_before=before_refs,unit_refs_after=read(unit+4)[0],
                    abi=f"ECX=unit, stack+4=order, stack+8={replacement_mode}, stack+c=1")
                floats(inputs,1/32)
                full_next=expected_task_codes[len(before_chain):]
                expected_task_codes=[t[2] for t in before_chain[:point_index+1]]+[0xd0144,0xd0162]+full_next*(1 if replacement_mode else 3)
                if replacement_mode:
                    second_identity=replacement_identity
                    next_target=replacement_target
            if replacement_target and not replacement_mode:
                # Every successor was admitted by the original completion dispatcher.
                # Pools recycle two group/path objects across all four admissions.
                while read(mover+0x9c,2)!=[0xffffffff]*2 and read(mover+0x9c,2)!=read(group+0x14,2):
                    new_group=next(g for g in group_objects if read(g+0x14,2)==read(mover+0x9c,2))
                    active_chain=chain()
                    assert active_chain[0][2]==0xd016b
                    active_target=[read(active_chain[0][0]+o)[0] for o in (0x38,0x40)]
                    assert read(new_group+0x4c,2)==[w-0x02800000 for w in active_target]
                    queue_transitions.append(dict(tick=tick,target_bits=active_target,
                        user_head=read(unit+0x19c,2),queue_count=read(unit+0x1b4)[0],
                        task_identity=active_chain[0][1],group_identity=read(new_group+0x14,2)))
                    run(0x6f16c150,group)
                    group=new_group
                    run(0x6f16c150,group)
            elif next_target and not queue_transitions and read(unit+0x19c,2)==second_identity:
                assert read(unit+0x1b4)[0]==1
                assert read(mover+0x9c,2)==read(second_group+0x14,2)
                assert read(second_group+0x4c,2)==[w-0x02800000 for w in next_target]
                second_chain=chain()
                assert second_chain[0][2]==0xd016b
                queue_transitions.append(dict(tick=tick,position_bits=want,second_task_identity=second_chain[0][1]))
                run(0x6f16c150,group)
                group=second_group;path=second_path
                run(0x6f16c150,group)
            if read(unit+0x174)[0]==0xffffffff:break
        else:raise AssertionError('generated point-order chain failed to arrive')
        assert read(unit+0x19c,2)==[0xffffffff]*2
        assert read(unit+0x1b4)[0]==0
        assert read(unit+0x1a8,2)==[0xffffffff]*2
        assert read(unit+0x194)[0]==0
        assert read(ability+0x20)[0]==0
        assert read(mover+0x9c,2)==[0xffffffff]*2
        all_task_events=[int(code,16) for address,code in dispatched if address=='0x6f071da0']
        actual_task_events=[c for c in all_task_events if c in [t[2] for t in before_chain]]
        assert actual_task_events==expected_task_codes,(actual_task_events,expected_task_codes)
        assert ("0x6f5fda10","0xd0166") in dispatched  # Additional init subscription is active.
        accepted['complete_dispatch']=list(dispatched)
        for hook in dispatch_hooks+progress_hooks:machine.hook_del(hook)
        if replacement_target and not replacement_mode:
            expected_targets=[list(replacement_target),list(target),list(next_target)]
            expected_identities=[replacement_identity,order_identity,second_identity]
            assert [a['target_bits'] for a in arrivals]==expected_targets
            assert [a['target_bits'] for a in admissions]==expected_targets
            assert [a['user_head'] for a in arrivals]==expected_identities
            assert [a['order_identity'] for a in admissions]==expected_identities
            assert [a['queue_count'] for a in admissions]==[3,2,1]
            assert admissions[0]['clock_bits']<arrivals[0]['clock_bits']
            assert [a['clock_bits'] for a in admissions[1:]]==[a['clock_bits'] for a in arrivals[:2]]
            assert queue_transitions[1]['task_identity']!=before_chain[point_index][1]
            assert [q['target_bits'] for q in queue_transitions]==expected_targets
            assert [q['queue_count'] for q in queue_transitions]==[3,2,1]
            replacement_evidence['original_point_task_identity']=before_chain[point_index][1]
            replacement_evidence['resumed_point_task_identity']=queue_transitions[1]['task_identity']
            accepted['prepend']=replacement_evidence
        else:
            assert [a['target_bits'] for a in arrivals]==([list(replacement_target)] if replacement_target else ([list(target),list(next_target)] if next_target else [list(target)]))
            assert arrivals[0]['user_head']==(second_identity if replacement_target else order_identity)
            if next_target:
                assert len(admissions)==1
                assert admissions[0]['order_identity']==second_identity
                assert admissions[0]['user_head']==second_identity and admissions[0]['queue_count']==1
                assert admissions[0]['target_bits']==list(next_target)
                if replacement_target:
                    assert admissions[0]['clock_bits']<arrivals[0]['clock_bits']
                    assert replacement_evidence
                    accepted['replacement']=replacement_evidence
                else:
                    assert admissions[0]['clock_bits']==arrivals[0]['clock_bits']
                    assert arrivals[1]['clock_bits']>=arrivals[0]['clock_bits']
                    assert arrivals[1]['user_head']==second_identity
            else:assert not admissions
        accepted.update(arrivals=arrivals,next_order_admission=admissions)
        assert read(mover+0x80,2)==[0,0]
        for _ in range(2):run(0x6f054190,inputs,edx=clock)
        run(0x6f16c150,group)
        assert read(group+0x38)[0]==0
        for offset,objects_to_free in [(0x678,group_objects),(0x958,path_objects)]:
            assert read(owner+offset+0x18)[0]==0
            free=[];ptr=read(owner+offset+0x14)[0]
            while ptr:
                assert ptr not in free
                free.append(ptr);ptr=read(ptr)[0]
            assert set(free)=={obj-4 for obj in objects_to_free}
        assert all(read(factory+0xc)[0]==0 for name,raw,factory,vt,psize in classes)
        free_payloads={}
        for n,(name,raw,factory,vt,psize) in enumerate(classes):
            free=set();ptr=read(factory+0x14)[0]
            while ptr:
                assert ptr not in free
                free.add(ptr);ptr=read(ptr)[0]
            assert free=={system+0x10000+n*0x1800+i*0xc0 for i in range(32)}
            free_payloads[name]=len(free)
        assert read(pool+0x18)[0]==0
        assert read(order_wrapper+0x14,4)==[0xffffffff,0xffffffff,0,0]
        assert read(order_wrapper+0x54)[0]==0
        assert read(registry+0x48)[0]==3
        assert read(unit+4)[0]==4
        assert read(clock+0x20)[0]==1
        assert read(unit+0x284,2)==[w+0x02800000 for w in trajectory[-1]['position']]
        if replacement_target:
            assert read(replacement_wrapper+0x14,4)==[0xffffffff,0xffffffff,0,0]
            assert read(replacement_wrapper+0x54)[0]==0
            assert read(unit+4)[0]==replacement_evidence['unit_refs_before']
            replacement_evidence['unit_refs_final']=read(unit+4)[0]
        accepted['reclaimed_payloads_by_class']=free_payloads
        if next_target:
            assert len(queue_transitions)==(3 if replacement_target and not replacement_mode else 1)
            assert read(second_wrapper+0x14,4)==[0xffffffff,0xffffffff,0,0]
            assert read(second_wrapper+0x54)[0]==0
            accepted.update(second_target_bits=next_target,queue_transitions=queue_transitions,completed_internal_tasks=len(expected_task_codes))
        accepted.update(initial_route=initial_route,trajectory=trajectory,arrival_tick=tick,
                        user_order_reclaimed=True)
        return accepted
    dispatch_cases=[]
    for flag in (0,1):
      for player in (0,12):
       for status in (0,0x10000):
        for target in [(0x43400000,0x43000000),(0x43000000,0x43800000),(0x437fffff,0x43400001)]:
            dispatch_cases.append(dispatch_case(flag,player,status,target))
    fifo_cases=[dispatch_case(0,0,0,(0x43400000,0x43000000),second)
                for second in [(0x43800000,0x43800000),(0x43000000,0x43000000),
                               (0x43000000,0x43800000),(0x43400001,0x43000000)]]
    prepend_cases=[dispatch_case(0,0,0,(0x43400000,0x43000000),(0x43800000,0x43800000),destination,0)
                   for destination in [(0x43000000,0x43800000),(0x43000000,0x43000000),(0x43800000,0x43000000)]]
    replacement_cases=[dispatch_case(0,0,0,(0x43400000,0x43000000),(0x43800000,0x43800000),destination)
                       for destination in [(0x43000000,0x43800000),(0x43000000,0x43000000),(0x43800000,0x43000000)]]
    report=dict(replacement_cases=replacement_cases,replacement_arrival_cases=len(replacement_cases),
      replacement_elapsed_ticks=sum(c['arrival_tick'] for c in replacement_cases),
      prepend_cases=prepend_cases,prepend_complete_cases=len(prepend_cases),
      prepend_elapsed_ticks=sum(c['arrival_tick'] for c in prepend_cases),
      prepend_completed_internal_tasks=sum(c['completed_internal_tasks'] for c in prepend_cases),binary_sha256=digest,passed=True,direct_point_producer_calls=direct_cases,
      full_move_order_producer_calls=len(full_cases),full_move_cases=full_cases,
      full_produced_chain_dispatch_calls=len(dispatch_cases),produced_chain_dispatch=dispatch_cases,
      complete_initial_admissions=len(dispatch_cases)+len(fifo_cases)+len(replacement_cases)+len(prepend_cases),
      queued_order_arrival_cases=len(dispatch_cases),elapsed_integration_ticks=sum(c['arrival_tick'] for c in dispatch_cases),
      completed_internal_tasks=sum(c['popped_tasks']+len(c['remaining_codes']) for c in dispatch_cases),
      crt_sha256=crt_digest,resolved_crt_exports=resolved_crt_exports,
      fifo_cases=fifo_cases,fifo_two_order_cases=len(fifo_cases),
      fifo_elapsed_integration_ticks=sum(c['arrival_tick'] for c in fifo_cases),
      fifo_completed_internal_tasks=sum(c['completed_internal_tasks'] for c in fifo_cases),
      evidence=dict(entry='5fd270',required_input_type='COrderTarget ordt, derived from COrderPoint',
        admission_modes=dict(entry='680320',abi='thiscall ECX unit; entryESP+4 order,+8 mode,+c dispatch flag; ret0xc',
          mode0='673fe0 active cancellation; 691c70 prepends and preserves userqueue',
          mode1='673610 clears active and pending userorders; 693490 appends replacement',
          original_callers={'654090':'observed widget call (order,0,1)', '233de0':'static call at233e17 (order,1,1); whole caller not executed'},
          restrictions='unit198=0; unit5c bit100 clear; 69b2f0 returns0; other mode branches excluded'),
        composed_initial_commands={'flag0':'d0012', 'flag1':'d0014;058040 zero normalizes later policy to0 after early d0165 omission'},
        task_producer='692120',factories=[dict(name=n,rawcode=hex(r),factory=hex(f),vtable=hex(v),size=s) for n,r,f,v,s in classes],
        wrapper_creation='055d00 -> 154b60 -> owner10 04c220 -> 057940 -> CAgentBaseAbs; owner254 04d9c0 binds actual factory payload',
        ordinary_point='order48/50 copied bitwise to task38/40; task48=+0; task30=d016b',
        prepend='Every produced task gets actual registry identity and links previous canonical head; creation order is reverse consumption order'),
      assertions=['All full calls return with ABI stack, callee-saved registers and FS exception chain intact',
        'Input order byte image restored unchanged including reference count',
        'Exact full task sequence, actual class vtables, payload references, consecutive canonical identities and previous chain preserved',
        'Standalone radius and coordinates are bit copies; retained optional object reference increments once per task',
        'First680320 admission executes original cancellation,693490 queue publication,67abe0 ability validation/dispatch,5fd270 production and67df00 acceptance without mid-call provisioning; exact independently predicted task sequence asserted',
        'Initial d0012 invokes flag0; d0014 invokes flag1 then058040 zero normalizes final policy to flag0; separate direct-handler corpus preserves unrestricted flag contracts',
        'Acceptance validates actual group/path identities, recycled-generator release, member ownership, target/center bits, speed8 and prepared scheduler flags',
        'Each clock step matches independent integer-bit add/multiply position integration',
        'Natural arrival drains every generated task and actual userqueue; original clock releases every task/order wrapper and restores all five factory free lists',
        'Following group tick returns actual group/path pools; only three original world identities remain live',
        'Actual693490 appends second order without altering active taskchain; automatic67de20/67abe0 invokes second5fd270 exactly at first arrival, with firsttaskchain fully drained and queuecount1',
        'Two arrival callbacks preserve FIFO target and userhead identities; both orders and every generated task are reclaimed',
        '680320 stack+8=1 replaces active and queued orders during tick3 travel; only replacement is admitted/arrives, all three orders reclaimed and unit reference count restored',
        '680320 stack+8=0 interrupts at tick3, completes new point, regenerates interrupted old point task with fresh identity, then completes pending successor; all three orders reclaimed and unit references restored'],
      boundaries=['Preallocated original factory/pool capacity and seeded class registry; external heap growth and global registration not executed',
        'COrderTarget is constructed and registered by original code; command fields are controlled fixture data, coordinates set through original FloatMini setters',
        'Ordinary point target only: invalid target identity and order68=0; target units/items/destructibles/waygates and local-target branch excluded',
        'UI notification singleton absent; stockhfoo terrain transform executes shippedCRT; user input/network admission remains separate',
        'Every composed first admission executes680320(order,1,1) with complete fixture; factory-created order command/coordinates remain controlled input and UI/network producer is excluded',
        'Group ticks are orchestrated explicitly; new successor group receives its first actual tick after predecessor group cleanup at the same simulation time',
        'Move-only attached behavior: registered ability list, original20event registration and Unitself action/state subscriptions; unrelated abilities/presentation subscribers absent',
        'Class caches: Amov-to-Bply/BUsl/Asla false and ordt-to-+ord true are preexisting fixture entries; cache-miss class construction excluded',
        '680320 mode1 tuple also exists at233de0/233e17 statically; its caller/input policy not executed; mode0 corresponds to original widget654090 call tuple. Unit198=0 and no69b2f0 rejection in tested modes',
        'Open16x16 fine grid; runtime obstacles, adaptive search and multiunit conflicts not included in this composed corpus',
        'Standalone raw signed-zero/subnormal/negative radius copying does not establish producer reachability',
        'Optional retained object tests use genuine registered COrderTarget as a raw reference-contract fixture, not a claim of gameplay target class reachability'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('full_move_cases','evidence','produced_chain_dispatch','resolved_crt_exports','fifo_cases','replacement_cases','prepend_cases')},indent=2))

if __name__ == '__main__': main()
