#!/usr/bin/env python3
"""Original COrderPoint/CTaskPoint construction, deferred release, last-reference reclamation and reuse.

Runs unmodified retail code with real vtables and preallocated allocator blocks.
No retail bytes, code patches, replacement callbacks or external allocator stubs.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_INVALID, UC_HOOK_CODE
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

    machine.mem_map(0x10000000, 0x20000)
    machine.mem_map(0x20000000, 0x10000)
    system = 0x10000000
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=100000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail lifetime call exceeded instruction budget')

    machine.mem_map(0, 0x1000)  # Original FS:[0] exception chain.
    host, owner, registry, slots, wrapper, payload, pool, request_block, heap, bucket, entry, hash_input = [
        system+n for n in (0,0x1000,0x2000,0x2100,0x3004,0x4004,0x5000,0x6000,0x6100,0x7000,0x7100,0x7200)]
    class_reports=[]
    for class_name,factory,factory_vtable,construct,reclaim,destructor,payload_vtable,payload_size,owned_offset,rawcode in [
        ('COrderPoint',0x6fd70e14,0x6fb7885c,0x6f6807a0,0x6f678db0,0x6f667400,0x6fb7886c,0x58,0x54,0x6f72642e),
        ('CTaskPoint',0x6fd70f1c,0x6fb78eb0,0x6f680db0,0x6f678ff0,0x6f667a80,0x6fb78ec0,0x50,0x4c,0x74736b2e),
    ]:
        allocator = factory+4
        write(0x6fd3c82c,host)
        write(0x6fd53a48,owner)
        write(0x6fd68610,registry)
        write(0x6fd3c744,0)
        write(factory,factory_vtable)  # Original class-specific factory vtable.
        run(0x6f06a270,allocator,payload_size,1)
        write(allocator+0x10,payload)
        write(payload,0)
        write(hash_input,rawcode)
        run(0x6f198420,hash_input)
        class_hash=machine.reg_read(UC_X86_REG_EAX)
        write(host+0x28,bucket,0,0)  # One bucket, mask zero.
        write(bucket,0,0,entry)
        write(entry+4,class_hash)
        write(entry+0x18,rawcode)
        write(entry+0x70,factory)
        write(host+0x34,pool)
        write(owner+0x254,0x6f04d9c0)
        callbacks=[]
        targets={0x6f04c2c0:'owner',0x6f03e9c0:'payload_release',reclaim:'factory_reclaim',
                 destructor:'destructor',0x6f06a3c0:'allocator_return',0x6f0576b0:'wrapper_return'}
        def observe(uc,address,size,data):
            callbacks.append((targets[address],read(payload+4)[0],read(payload+0xc,2)))
        hooks=[machine.hook_add(UC_HOOK_CODE,observe,begin=address,end=address) for address in targets]
        machine.ctl_flush_tb()
        cases=[]
        for cycle in range(4):
            run(construct,factory)
            assert machine.reg_read(UC_X86_REG_EAX)==payload
            assert read(payload)[0]==payload_vtable
            assert read(payload+4,4)==[0,0,0xffffffff,0xffffffff]
            assert read(payload+owned_offset)[0]==0
            retained_object=system+0x9000
            machine.mem_write(retained_object,bytes(0x80))
            write(retained_object,payload_vtable,2)
            if cycle%2:
                write(payload+owned_offset,retained_object)
            retained_before=bytes(machine.mem_read(retained_object,0x80))
            assert read(allocator+8)[0]==1 and read(allocator+0x10)[0]==0
            machine.mem_write(wrapper,bytes(0x100))
            write(wrapper,0x6fa8099c)
            write(wrapper+0xc,0x2b61676c,0x2b616761,0,100+cycle)
            write(wrapper+0x54,payload)
            write(payload+4,1)
            write(payload+0xc,0,100+cycle)
            write(registry+0xc,slots)
            write(registry+0x1c,1)
            write(registry+0x40,-1)
            write(registry+0x48,1)
            write(slots,-2,wrapper)
            write(pool+0x14,0,1)
            clock=owner+0x14
            machine.mem_write(clock,bytes(0x54))
            machine.mem_write(request_block,bytes(0x100))
            machine.mem_write(heap,bytes(0x100))
            write(clock+0x10,heap)
            write(clock+0x1c,16,1)
            write(clock+0x38,request_block)
            write(clock+0x40,0x3f000000)
            run(0x6f0557b0,payload)
            request=request_block+4
            assert read(wrapper+0x20)[0]==request
            deadline=read(request+4)[0]
            run(0x6f0557b0,payload)
            assert read(clock+0x3c)[0]==1 and read(clock+0x50)[0]==1
            start=len(callbacks)
            run(0x6f052380,clock)
            assert len(callbacks)==start
            write(clock+0x40,deadline if cycle%2 else 0x3f400000)
            saved_time=read(clock+0x40)[0]
            run(0x6f052380,clock)
            trace=callbacks[start:]
            assert [x[0] for x in trace]==['owner','payload_release','factory_reclaim','destructor','allocator_return','wrapper_return'],trace
            assert trace[0][1:]==(1,[0,100+cycle])
            assert all(x[1:]==(0,[0xffffffff,0xffffffff]) for x in trace[1:5]),trace
            assert read(0x6fcd574c)[0]==0x40190064
            assert read(0x6fcd5754)[0]==payload
            assert read(request+0x10)[0]==0x10000
            assert read(allocator+8)[0]==0 and read(allocator+0x10)[0]==payload
            assert read(payload)[0]==0
            assert read(retained_object+4)[0]==2-(cycle%2)
            retained_after=bytes(machine.mem_read(retained_object,0x80))
            assert retained_after[:4]==retained_before[:4] and retained_after[8:]==retained_before[8:]
            assert read(slots,2)==[0xffffffff,0]
            assert read(registry+0x40)[0]==0 and read(registry+0x48)[0]==0
            assert read(wrapper+0x14,4)==[0xffffffff,0xffffffff,0,0]
            assert read(wrapper+0x50,2)==[0,0]
            assert read(pool+0x14,2)==[wrapper-4,0]
            assert read(clock+0x38,2)==[request_block,0]
            assert read(clock+0x20)[0]==1 and read(clock+0x40)[0]==saved_time
            assert read(0)[0]==0
            snapshot=bytes(machine.mem_read(system,0x8000))+bytes(machine.mem_read(allocator,0x14))
            run(0x6f052380,clock)
            assert bytes(machine.mem_read(system,0x8000))+bytes(machine.mem_read(allocator,0x14))==snapshot
            assert len(callbacks)==start+6
            cases.append(dict(cycle=cycle,callbacks=[x[0] for x in trace],deadline_bits=deadline))
        for hook in hooks: machine.hook_del(hook)
        class_reports.append(dict(name=class_name,zero_reference_release_cases=len(cases),factory_reuse_cases=len(cases)-1,class_rawcode=hex(rawcode),class_hash=hex(class_hash),cases=cases))
    report=dict(binary_sha256=digest,passed=True,scope=__doc__,
                zero_reference_release_cases=sum(row['zero_reference_release_cases'] for row in class_reports),
                factory_reuse_cases=sum(row['factory_reuse_cases'] for row in class_reports),retained_owned_object_release_cases=4,classes=class_reports,
                boundaries=['Preallocated CDataAllocator blocks; external heap growth not executed',
                            'Manually registered wrapper and class table; live wrapper/class registration not executed',
                            'Empty payload subscriptions/relationships/children; point-owned object null or retained ref2→1, final owned-object destruction untested',
                            'Positive handle domain; no nested callbacks or nonempty request heap'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
