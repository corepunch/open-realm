#!/usr/bin/env python3
"""Original cached-exit consumer controls; owner lookup and physical placement
are explicit stand-ins. Executes165d10/166030/165200/167ae0 and scalar math.
Public engine trajectories validate physical placement separately.
"""
import argparse
import ctypes
import hashlib
import json
import itertools
import struct
from pathlib import Path



def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture',type=Path)
    parser.add_argument('--engine-library',type=Path)
    args=parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve()))if args.engine_library else None
    exact_requests=0
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

    machine.mem_map(0x10000000, 0x400000)
    machine.mem_map(0x20000000, 0x10000)
    system, maps = 0x10000000, [0x10001000 + i * 0x100 for i in range(4)]
    data = [0x10010000 + i * 0x10000 for i in range(4)]
    nodes, heap, route, route_data = 0x10080000, 0x10100000, 0x10200000, 0x10201000
    source_ptr, target_ptr = 0x10000400, 0x10000410
    stack, stop, width = 0x20008000, 0x30000000, 32
    start, goal = (4, 4), (27, 27)
    source, target = (6.25, 6.75), (27.25, 27.75)
    constants = {'6fd3c740': -1.0, '6fd3c744': 0.0, '6fd3c748': 1.0, '6fd53a74': -128000.0078125}
    for address, value in constants.items():
        machine.mem_write(int(address, 16), struct.pack('<f', value))

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
            raise RuntimeError('retail adaptive operation exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    path,points,output,special=0x10300000,0x10301000,0x10302000,0x10303000
    write(system+0x3c,special);write(system+0x1c,maps[0]);machine.mem_write(maps[0]+0x64,struct.pack('<f',2))
    machine.mem_write(0x6fd54194,struct.pack('<f',10));write(path+0x60,points)
    placed=[];placement_result=0
    def return_call(uc,cleanup,result):
        sp=uc.reg_read(UC_X86_REG_ESP);ret=read(sp)[0];uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_ESP,sp+4+cleanup);uc.reg_write(UC_X86_REG_EIP,ret)
    def owner_lookup(uc,address,size,data):return_call(uc,0,system)
    def physical_place(uc,address,size,data):
        sp=uc.reg_read(UC_X86_REG_ESP);placed.append(read(read(sp+4)[0],2));return_call(uc,4,placement_result)
    machine.hook_add(UC_HOOK_CODE,owner_lookup,begin=0x6f168d10,end=0x6f168d10)
    machine.hook_add(UC_HOOK_CODE,physical_place,begin=0x6f168f00,end=0x6f168f00)
    if engine:engine.pathing_adaptive_gate_threshold.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*3
    cases=[]
    current=0x10304000
    for point in ((0.,0.),(7.75,8.75)):
     nominal=struct.unpack('<I',struct.pack('<f',point[0]+struct.unpack('<f',struct.pack('<I',0x3efae148))[0]))[0]
     for source_word in range(nominal-2,nominal+3):
      current_words=[source_word,struct.unpack('<I',struct.pack('<f',point[1]))[0]]
      write(current,*current_words)
      route_points=[(27.25,27.75),(-128000.0078125,1.),point,(4.25,4.75)]
      route_words=list(struct.unpack('<8I',struct.pack('<8f',*[v for p in route_points for v in p])))
      write(points,*route_words);write(path+0x70,len(route_points))
      for index,force,active,placement_result in itertools.product((0,2),(0,1),(0,1,2,3),(0,1)):
       for gate_id in range(256):write(special+12*gate_id,active,27,27)
       write(path+0x74,7,index);write(path+0x94,5);placed.clear()
       result=run(0x6f165f10,path,current,force)
       if engine:
        q=(ctypes.c_uint32*9)(len(route_points),index,force,active,placement_result,*current_words,7,5)
        out=(ctypes.c_uint32*7)();engine.pathing_adaptive_gate_threshold(q,(ctypes.c_uint32*len(route_words))(*route_words),out)
        wanted=[result&255,read(path+0x78)[0],read(path+0x74)[0],read(path+0x94)[0],len(placed),*(placed[0] if placed else [0,0])]
        if list(out)!=wanted:raise RuntimeError('C threshold/consumer state differs: '+str((list(out),wanted)))
        exact_requests+=1
       cases.append(dict(point=list(point),source=current_words,force=force,index=index,active=active,placement_result=placement_result,result=result&255,next_index=read(path+0x78)[0],fine_index=read(path+0x74)[0],delay=read(path+0x94)[0],placed=list(placed)))
    report=dict(binary_sha256=digest,cases=cases,scope='Original165f10 approach threshold and165d10/166030/165200/167ae0 consumer plus scalar math.168d10 owner lookup and168f00 placement are explicit stand-ins; public birth/rebind/ground restoration captures validate production eligibility and placement separately. The only direct165b60 caller supplies force0; force1 is a controlled helper boundary.')
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text())!=report:raise RuntimeError('Frozen original threshold differs')
        else:args.fixture.write_text(json.dumps(report,separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(dict(report,passed=True,engine_exact_requests=exact_requests),indent=2)+'\n')
    print('original approach threshold controls',len(cases))

if __name__=='__main__':main()
