#!/usr/bin/env python3
"""Original mover speed/heading update with exact binary-fraction inputs.

No stubs or retail bytes. Tests speed/heading update, angle-error composition and parameter setters;
position integration, group decisions/formations/completion, deferred release, and full
arrival dispatch with actual terrain support refresh, task completion, next-task acceptance/rejection,
fresh fine/adaptive searches, elapsed obstacle detours, owner scheduling/visual settling,
and controlled two-mover separation with an active-order eligibility composition.
See report counts/exclusions.
"""
import argparse
import hashlib
import gzip
import itertools
import math
import json
import struct
import ctypes
from pathlib import Path
from verify_wc3_pathing_numeric import add as float_add, multiply as float_multiply, subtract as float_subtract, bits as float_bits
from verify_wc3_pathing_grid import reference as reference_grid


def swept_cell_witness(a,b,cell):
    """Independent closest pair for a center segment and one blocked unit square."""
    delta=[b[k]-a[k] for k in range(2)]
    low,high=0.0,1.0
    for k in range(2):
        if delta[k]==0:
            if not cell[k]<=a[k]<=cell[k]+1:low,high=1.0,0.0
        else:
            t0,t1=sorted([(cell[k]-a[k])/delta[k],(cell[k]+1-a[k])/delta[k]])
            low,high=max(low,t0),min(high,t1)
    def candidate(t,wall_point):
        point=[a[k]+t*delta[k] for k in range(2)]
        return dict(distance=math.hypot(*(point[k]-wall_point[k] for k in range(2))),
                    cell=cell,segment_fraction=t,center_point=point,wall_point=wall_point)
    if low<=high:
        point=[a[k]+low*delta[k] for k in range(2)]
        return candidate(low,point)
    choices=[candidate(t,[max(cell[k],min(cell[k]+1,p[k])) for k in range(2)]) for t,p in [(0,a),(1,b)]]
    length2=sum(v*v for v in delta)
    for x,y in itertools.product([cell[0],cell[0]+1],[cell[1],cell[1]+1]):
        corner=(x,y)
        t=max(0,min(1,sum((corner[k]-a[k])*delta[k] for k in range(2))/length2)) if length2 else 0
        choices.append(candidate(t,corner))
    return min(choices,key=lambda row:row['distance'])


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_INVALID
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, help='compiled wc3_pathing_engine_probe.c; compare decision/angle bits')
    parser.add_argument('--world-velocity-fixture', type=Path, help='export world-adapted original velocity/facing words including small-speed cutoffs')
    parser.add_argument('--facing-fixture', type=Path, help='export original committed facing inputs/results')
    parser.add_argument('--formation-refresh-fixture', type=Path, help='export member arrival refresh and regroup boundaries, then stop before unrelated lifecycles')
    parser.add_argument('--formation-fixture', type=Path, help='export complete original formation inputs/offset words')
    parser.add_argument('--native-pose-fixture', type=Path, help='export retained fine-pose sequences and original world inverse')
    parser.add_argument('--clock-trajectory-fixture', type=Path, help='export original primary-clock and old-velocity trajectory')
    parser.add_argument('--position-fixture', type=Path, help='export original world-position bridge writes')
    parser.add_argument('--heading-fixture', type=Path, help='export raw original vector-heading errors for asset-free C replay')
    parser.add_argument('--primary-route-fixture', type=Path, help='export full original owner detour on six authentic 5 ms advances per pass')
    parser.add_argument('--primary-route-reference', type=Path, help='repeat and compare original owner/clock/detour words against the frozen fixture')
    parser.add_argument('--route-trajectory-fixture', type=Path, help='export complete controlled wall-route decisions and original mover parameters')
    parser.add_argument('--route-trajectory-reference', type=Path, help='check complete controlled wall-route words against the frozen fixture')
    parser.add_argument('--completion-reference', type=Path, help='compare the complete original completion export against a frozen fixture')
    parser.add_argument('--completion-fixture', type=Path, help='export complete original blocked-completion boundaries, then stop before unrelated order lifecycles')
    args = parser.parse_args()
    if args.completion_reference and not args.completion_fixture:parser.error('--completion-reference requires --completion-fixture')
    if args.completion_fixture and args.completion_fixture.exists():parser.error('completion export must be fresh')
    if args.world_velocity_fixture and not args.engine_library:parser.error('--world-velocity-fixture requires --engine-library for exact guard comparisons')
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_velocity_world_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_velocity_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_velocity_heading.argtypes = [ctypes.c_uint32]*3
        engine.pathing_velocity_heading.restype = ctypes.c_uint32
        engine.pathing_facing_angle.argtypes = [ctypes.c_uint32]
        engine.pathing_facing_angle.restype = ctypes.c_uint32
        engine.pathing_velocity.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_integrate.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_angle.argtypes = [ctypes.c_uint32]
        engine.pathing_angle.restype = ctypes.c_uint32
        engine.pathing_heading_error.argtypes = [ctypes.c_uint32]*3
        engine.pathing_heading_error.restype = ctypes.c_uint32
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
    system, output = 0x10000000, 0x10000800
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=2000000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError(f'retail target lookup exceeded instruction budget at {machine.reg_read(UC_X86_REG_EIP):#x}')

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
    imports={'_libm_sse2_sin_precise':0x6fa7c48c,'_libm_sse2_cos_precise':0x6fa7c47c,'_libm_sse2_sqrt_precise':0x6fa7c4f8,'_CIatan2':0x6fa7c450,'_libm_sse2_asin_precise':0x6fa7c44c,'isdigit':0x6fa7c4fc}
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

    mover,speed_ptr,heading_ptr,delta_ptr=[system+n for n in (0x1000,0x2000,0x2010,0x2020)]
    machine.mem_write(0x6fd3c740,struct.pack('<3f',-1,0,1))
    def floats(address,*values):
        machine.mem_write(address,struct.pack('<'+'f'*len(values),*values))
    def scalar(address):
        return struct.unpack('<f',machine.mem_read(address,4))[0]
    cases=0
    for speed,increment,turn_cap,threshold,delta,blocked,heading in itertools.product(
            [0,0.125,1,8],[0,0.125,0.5,2],[0,0.125,0.5,2],[0,0.125,0.5,2],
            [-4,-2,-0.5,-0.125,0,0.125,0.5,2,4],[0,1],[-4,0,4]):
        floats(mover+0x88,0.25)  # helper itself must not clamp to mover max speed
        floats(mover+0xb4,increment,turn_cap,threshold)
        floats(speed_ptr,speed)
        floats(heading_ptr,heading)
        floats(delta_ptr,delta)
        run(0x6f170880,mover,speed_ptr,heading_ptr,delta_ptr,blocked)
        if engine:
            words = (ctypes.c_uint32 * 7)(*[float_bits(v) for v in (speed,heading,delta,increment,turn_cap,threshold)], blocked)
            engine.pathing_motion(words)
            assert list(words)[:2] == [read(speed_ptr)[0], read(heading_ptr)[0]]
        wanted_speed=0 if blocked or abs(delta)>=threshold else speed+increment
        wanted_heading=heading+max(-turn_cap,min(delta,turn_cap))
        assert scalar(speed_ptr)==wanted_speed,(speed,increment,threshold,delta,blocked)
        assert scalar(heading_ptr)==wanted_heading,(heading,turn_cap,delta)
        assert scalar(delta_ptr)==delta
        assert [scalar(mover+n) for n in [0xb4,0xb8,0xbc]]==[increment,turn_cap,threshold]
        cases+=1
    # Full handle-resolving speed producer clamps to global max, converts /32,
    # and writes both maximum speed and per-update speed increment.
    wrapper,registry,slots,config=[system+n for n in (0x3000,0x4000,0x4100,0x5000)]
    write(0x6fd68610,registry)
    write(registry+0xc,slots)
    write(registry+0x1c,1)
    write(slots,-2,mover)
    write(mover+0x14,0,100)
    write(wrapper+8,0,100)
    write(0x6fd3c82c,config)
    speed_producer_cases=0
    for cap,speed in itertools.product([32,100,320,522],[0,1,16,32,100,256,522,1000]):
        floats(config+0x80,cap)
        floats(speed_ptr,speed)
        floats(mover+0x88,-99)
        floats(mover+0xb4,-99)
        run(0x6f05c5c0,wrapper,speed_ptr)
        expected=min(speed,cap)/32
        assert scalar(mover+0x88)==expected
        assert scalar(mover+0xb4)==expected
        assert scalar(speed_ptr)==speed
        speed_producer_cases+=1
    # Full heading error: software length, acos-based vector heading, shortest
    # signed difference and deadzone. Independent atan2 checks numerical error;
    # the optional C kernel additionally compares every output word exactly.
    vector,angle_out=system+0x6000,system+0x6100
    pi=scalar(0x6fcd545c); tau=scalar(0x6fcd5464)
    deadzone=scalar(0x6fcd5470)
    def f32(value):
        return struct.unpack('<f',struct.pack('<f',value))[0]
    angle_cases=0
    heading_fixture=[]
    max_angle_error=0
    for x,y,heading in itertools.product([-4,-1,-0.125,0,0.125,1,4],
                                        [-4,-1,-0.125,0,0.125,1,4],
                                        [0,0.125,0.5,1,2,3,4,5,6]):
        floats(vector,x,y)
        floats(heading_ptr,heading)
        machine.reg_write(UC_X86_REG_EDX,heading_ptr)
        run(0x6f16f630,angle_out,vector)
        theta=math.atan2(y,x) if x or y else 0
        if theta<0: theta+=2*math.pi
        delta=theta-heading
        if delta>math.pi: delta-=2*math.pi
        elif delta<-math.pi: delta+=2*math.pi
        if abs(delta)<deadzone: delta=0
        actual=scalar(angle_out)
        if engine:
            assert engine.pathing_heading_error(*read(vector,2),read(heading_ptr)[0]) == read(angle_out)[0]
        heading_fixture.append([*read(vector,2),read(heading_ptr)[0],read(angle_out)[0]])
        error=abs(actual-delta)
        # Sign at exactly opposite headings depends on the represented pi.
        if abs(abs(delta)-math.pi)<1e-6:
            error=abs(abs(actual)-math.pi)
        assert error<0.0003,(x,y,heading,actual,delta,error)
        max_angle_error=max(max_angle_error,error)
        angle_cases+=1
    # Non-binary directions across all quadrants and the near-cardinal curve;
    # length thresholds include adjacent represented values. These exact checks
    # intentionally have no host atan2 tolerance assumption.
    heading_boundary_cases=0
    raw_vectors=[]
    for x,y in itertools.product([0.1,0.3,0.7,3.14159,17.23,1000.1],
                                 [0.0001,0.001,0.01,0.03,0.1,0.2,0.7,1.3]):
        for sx,sy in itertools.product([-1,1],repeat=2):
            raw_vectors.append([float_bits(sx*x),float_bits(sy*y)])
    raw_vectors.extend([[sign|word,0] for sign in [0,0x80000000]
                       for word in range(0x3727c5ac-4,0x3727c5ac+5)])
    for raw,heading in itertools.product(raw_vectors,[0,0.345,pi,tau]):
        write(vector,*raw)
        floats(heading_ptr,heading)
        machine.reg_write(UC_X86_REG_EDX,heading_ptr)
        run(0x6f16f630,angle_out,vector)
        if engine:
            assert engine.pathing_heading_error(*raw,read(heading_ptr)[0]) == read(angle_out)[0], (raw,heading)
        heading_boundary_cases+=1
    if args.heading_fixture:
        args.heading_fixture.parent.mkdir(parents=True,exist_ok=True)
        args.heading_fixture.write_text(json.dumps(dict(version=1,binary_sha256=digest,
            source_entry='6f16f630',columns=['x','y','current_heading','error'],cases=heading_fixture),
            separators=(',',':'))+'\n')
    # Exact principal-range producer values avoid wrap-rounding assumptions.
    turn_producer_cases=0
    minimum=scalar(0x6fcd53a0)
    for value in [0,0.0001,0.001,0.125,0.5,1,2,3,4,5,6]:
        floats(delta_ptr,value)
        encoded=scalar(delta_ptr)
        run(0x6f05c8c0,wrapper,delta_ptr)
        assert scalar(mover+0xb8)==max(encoded,minimum)
        run(0x6f05c890,wrapper,delta_ptr)
        assert scalar(mover+0xbc)==encoded
        turn_producer_cases+=2
    deadzone_cases=0
    threshold_bits=read(0x6fcd5470)[0]
    for sign,bits in itertools.product([0,0x80000000],[threshold_bits-1,threshold_bits,threshold_bits+1]):
        write(heading_ptr,sign|bits)
        floats(vector,4,0)
        machine.reg_write(UC_X86_REG_EDX,heading_ptr)
        run(0x6f16f630,angle_out,vector)
        heading=scalar(heading_ptr)
        assert scalar(angle_out)==(0 if abs(heading)<deadzone else -heading)
        if engine:assert engine.pathing_heading_error(*read(vector,2),read(heading_ptr)[0])==read(angle_out)[0]
        deadzone_cases+=1
    normalization=[]
    for value in [-2*tau,-tau,-0.5,0,0.5,tau,2*tau,10,-10]:
        floats(delta_ptr,value)
        machine.reg_write(UC_X86_REG_EDX,delta_ptr)
        run(0x6f062930,angle_out)
        if engine:
            assert engine.pathing_angle(read(delta_ptr)[0]) == read(angle_out)[0], value
        actual=scalar(angle_out)
        expected=scalar(delta_ptr)%(2*math.pi)
        error=abs(actual-expected)
        error=min(error,abs(error-2*math.pi))
        assert error<0.00001,(value,actual,expected)
        normalization.append(dict(input=scalar(delta_ptr),output=actual,circular_error=error))
        run(0x6f05c8c0,wrapper,delta_ptr)
        assert scalar(mover+0xb8)==max(actual,minimum)
        run(0x6f05c890,wrapper,delta_ptr)
        assert scalar(mover+0xbc)==actual
        turn_producer_cases+=2
    # Complete lazy position integration with both real spatial bound updates.
    owner=system+0x3000
    objects=[system+0x4000,system+0x4100]
    maps=[system+0x5000,system+0x5200]
    cells=[system+0x6000,system+0x7000]
    links=[system+0x8000,system+0xa000]
    bitmaps=[system+0xc000,system+0xc100]
    displacement=system+0xd000
    write(0x6fd53a48,owner)
    floats(0x6fd3c74c,2)
    time_deadzone=scalar(0x6fcd539c)
    engine_integrations=0
    def compare_integration(mover,clock,displacement):
        nonlocal engine_integrations
        if not engine:return None
        words=(ctypes.c_uint32*11)(*read(mover+0x78,4),*read(mover+0x70,2),
                                   *read(clock+0x40,3),*read(displacement,2))
        engine.pathing_integrate(words)
        engine_integrations+=1
        return list(words)
    def check_integration(mover,words):
        if words is None:return
        assert read(mover+0x78,2)==words[:2]
        assert read(mover+0x70,2)==words[4:6]
    integration_cases=0
    for domain,now,old,epoch,velocity,delta,radius in itertools.product(
            [0,0x80000000],[0.125,0.5,1,2],[0,0.125,0.5],[0,1],
            [-0.125,0,0.125],[0,0.25],[0.25,0.75,1.25,1.75]):
        machine.mem_write(owner,bytes(0x100))
        clock=owner+(0x68 if domain else 0x14)
        floats(clock+0x40,now)
        write(clock+0x44,epoch)
        floats(clock+0x48,8)
        write(mover+0x14,domain)
        floats(mover+0x70,old)
        write(mover+0x74,0)
        floats(mover+0x78,8,8,velocity,-velocity)
        floats(mover+0x90,radius)
        write(mover+0x94,*objects)
        floats(displacement,delta,-delta)
        for obj,grid,data,records,bitmap in zip(objects,maps,cells,links,bitmaps):
            machine.mem_write(obj,bytes(0x80))
            write(obj+0x2c,grid)
            write(obj+0x34,0x01000001)
            machine.mem_write(grid,bytes(0x100))
            write(grid+0x28,data)
            write(grid+0x3c,16,16)
            write(grid+0x54,0,0,16,16)
            floats(grid+0x68,1)
            write(grid+0x78,records)
            write(grid+0x84,1024,0)
            write(grid+0x98,bitmap)
            write(grid+0xac,0xffffff)
            machine.mem_write(bitmap,bytes(32))
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
        elapsed=now-old
        if abs(elapsed)<time_deadzone: elapsed=0
        elapsed+=epoch*8
        wanted=(8+velocity*elapsed+delta,8-velocity*elapsed-delta)
        engine_words=compare_integration(mover,clock,displacement)
        run(0x6f1603d0,mover,displacement)
        check_integration(mover,engine_words)
        assert (scalar(mover+0x78),scalar(mover+0x7c))==wanted
        assert scalar(mover+0x70)==now and read(mover+0x74)[0]==epoch
        assert (scalar(mover+0x80),scalar(mover+0x84))==(velocity,-velocity)
        # Verify actual new occupancy rectangles and inserted cell counts.
        x,y=wanted
        proximity=[math.floor(y-radius),math.floor(x-radius),math.floor(y+radius)+1,math.floor(x+radius)+1]
        cls=int(radius>=0.5)+int(radius>=1)+int(radius>=1.5)
        offset=[0,1,1,2][cls]; edge=cls+1
        fine=[math.floor(y)-offset,math.floor(x)-offset,math.floor(y)-offset+edge,math.floor(x)-offset+edge]
        for obj,grid,bounds in zip(objects,maps,[proximity,fine]):
            assert read(obj+0x1c,4)==bounds,(radius,wanted,read(obj+0x1c,4),bounds)
            assert read(grid+0x88)[0]==(bounds[2]-bounds[0])*(bounds[3]-bounds[1]),(hex(grid),bounds,read(grid+0x88)[0],read(grid+0xb0)[0])
        old_bounds=[proximity,fine]
        old_counts=[read(grid+0x88)[0] for grid in maps]
        floats(clock+0x40,now+0.25)
        floats(displacement,-delta,delta)
        wanted2=(wanted[0]+velocity*0.25-delta,wanted[1]-velocity*0.25+delta)
        engine_words=compare_integration(mover,clock,displacement)
        run(0x6f1603d0,mover,displacement)
        check_integration(mover,engine_words)
        assert (scalar(mover+0x78),scalar(mover+0x7c))==wanted2
        assert scalar(mover+0x70)==now+0.25 and read(mover+0x74)[0]==epoch
        x,y=wanted2
        new_bounds=[[math.floor(y-radius),math.floor(x-radius),math.floor(y+radius)+1,math.floor(x+radius)+1],
                    [math.floor(y)-offset,math.floor(x)-offset,math.floor(y)-offset+edge,math.floor(x)-offset+edge]]
        def occupied(bounds):
            return {(x,y) for y in range(bounds[0],bounds[2]) for x in range(bounds[1],bounds[3])}
        for obj,grid,data,record_data,old_rect,new_rect,old_count in zip(objects,maps,cells,links,old_bounds,new_bounds,old_counts):
            assert read(obj+0x1c,4)==new_rect
            wanted_cells=occupied(new_rect)
            assert read(grid+0x88)[0]==old_count+len(occupied(old_rect)^wanted_cells)
            actual_cells=set()
            for y in range(16):
                for x in range(16):
                    index=read(data+(y*16+x)*4)[0]&0xffffff
                    if index!=0xffffff:
                        packed,payload=read(record_data+index*8,2)
                        assert payload==obj and packed>>24 in [0,1]
                        if packed>>24==1: actual_cells.add((x,y))
            assert actual_cells==wanted_cells
        integration_cases+=1
    time_sequences=0
    for domain,now_epoch,old_epoch,now,old in itertools.product(
            [0,0x80000000],[0,1,2],[0,1,2],[0,0.25,0.5],[0,0.125]):
        clock=owner+(0x68 if domain else 0x14)
        write(mover+0x14,domain)
        floats(clock+0x40,now)
        write(clock+0x44,now_epoch)
        floats(clock+0x48,8)
        floats(mover+0x70,old)
        write(mover+0x74,old_epoch)
        run(0x6f161040,mover,angle_out)
        assert scalar(angle_out)==now-old+(now_epoch-old_epoch)*8
        assert scalar(mover+0x70)==old and read(mover+0x74)[0]==old_epoch
        run(0x6f161090,mover)
        assert scalar(mover+0x70)==now and read(mover+0x74)[0]==now_epoch
        run(0x6f161040,mover,angle_out)
        assert scalar(angle_out)==0
        time_sequences+=1
    time_boundary_cases=0
    boundary=read(0x6fcd539c)[0]
    for domain,sign,bits in itertools.product([0,0x80000000],[0,0x80000000],[boundary-1,boundary,boundary+1]):
        clock=owner+(0x68 if domain else 0x14)
        write(mover+0x14,domain)
        write(clock+0x40,sign|bits,0)
        write(mover+0x70,0,0)
        run(0x6f161040,mover,angle_out)
        now=scalar(clock+0x40)
        assert scalar(angle_out)==(0 if abs(now)<time_deadzone else now)
        time_boundary_cases+=1
    facing_cases=[]
    for x,y,facing in itertools.product([-100,-4,-.001,-.0005,0,.0005,.001,4,100],repeat=3):
        floats(mover+0x80,x,y,8,facing)
        original=read(mover+0x80,4)
        run(0x6f160060,mover)
        facing_cases.append(original+[read(mover+0x8c)[0]])
    floats(mover+0x80,100,0,100,.125)
    write(mover+0x80,0x42c67084,0x41477a18)
    original=read(mover+0x80,4);run(0x6f160060,mover)
    facing_cases.append(original+[read(mover+0x8c)[0]])
    boundary_vectors=[(0x3a25cb5f+delta,0) for delta in range(-8,9)]
    boundary_vectors += [(0x3a176b4c,0x39870e5f+delta) for delta in (-1,0,1)]
    for (x,y),sign,before in itertools.product(boundary_vectors,[0,0x80000000],[0,0x3e000000]):
        write(mover+0x80,x|sign,y,0x41000000,before)
        original=read(mover+0x80,4);run(0x6f160060,mover)
        facing_cases.append(original+[read(mover+0x8c)[0]])
    if engine:
        for x,y,maximum,before,after in facing_cases:
            assert engine.pathing_velocity_heading(x,y,before)==after
    facing_angles=[float_bits(x) for x in [-100,-2*pi,-pi,-.125,-0.0,0,.125,pi,2*pi,100]]
    facing_angles += [word|sign for word in range(0x40c90fdb-8,0x40c90fdb+9) for sign in (0,0x80000000)]
    facing_angle_outputs=[]
    for word in facing_angles:
        write(heading_ptr,word);run(0x6f15ffd0,mover,heading_ptr)
        result=read(mover+0x8c)[0];facing_angle_outputs.append([word,result])
        if engine:assert engine.pathing_facing_angle(word)==result,hex(word)
    if args.facing_fixture:
        args.facing_fixture.write_text(json.dumps(dict(version=1,binary_sha256=digest,columns=['vx','vy','maximum','before_facing','after_facing'],cases=facing_cases,angles=facing_angle_outputs),separators=(',',':'))+'\n')
    velocity_cases=[];world_velocity_cases=[]
    velocity_speeds=[0,0.00025,0.0003125,0.0005,0.125,1,4,100,256]
    velocity_speeds += [struct.unpack('<f',struct.pack('<I',float_bits(math.sqrt(2e-7))+d))[0] for d in range(-8,9)]
    for new_speed,new_heading,maximum,old_velocity in itertools.product((velocity_speeds if engine else [0,0.125,1,4]),[0,0.125,pi/2,pi,3*pi/2],[0,0.5,2,8],[(0.125,-0.125),(0,0)] if engine else [(0.125,-0.125)]):
        write(mover,0x6fa9129c,owner+0x200,0)  # actual retail vtable, already on update list
        write(mover+0x14,0)
        clock=owner+0x14
        floats(clock+0x40,0.5)
        write(clock+0x44,0)
        floats(clock+0x48,8)
        floats(mover+0x70,0)
        write(mover+0x74,0)
        floats(mover+0x78,8,8,*old_velocity,maximum,0.125 if old_velocity==(0,0) else 0)
        floats(mover+0x90,0.25)
        write(mover+0x94,*objects)
        for obj,grid,data,records,bitmap in zip(objects,maps,cells,links,bitmaps):
            machine.mem_write(obj,bytes(0x80))
            write(obj+0x2c,grid)
            write(obj+0x34,0x01000001)
            machine.mem_write(grid,bytes(0x100))
            write(grid+0x28,data)
            write(grid+0x3c,16,16)
            write(grid+0x54,0,0,16,16)
            floats(grid+0x68,1)
            write(grid+0x78,records)
            write(grid+0x84,1024,0)
            write(grid+0x98,bitmap)
            write(grid+0xac,0xffffff)
            machine.mem_write(bitmap,bytes(32))
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
        floats(speed_ptr,new_speed)
        floats(heading_ptr,new_heading)
        original_velocity=read(mover+0x80,2)+[read(speed_ptr)[0],read(heading_ptr)[0]]+read(mover+0x88,2)
        world_input=[float_multiply(w,0x42000000) if n in (0,1,2,4) else w for n,w in enumerate(original_velocity)]
        if engine:
            engine_vel=(ctypes.c_uint32*6)(*read(mover+0x80,2),read(speed_ptr)[0],read(heading_ptr)[0],*read(mover+0x88,2))
            engine.pathing_velocity_commit(engine_vel)
        run(0x6f16fe20,mover,speed_ptr,heading_ptr)
        if engine:
            assert read(mover+0x80,2)==list(engine_vel)[:2],(new_speed,new_heading,maximum)
            assert read(mover+0x8c)[0]==engine_vel[5],(new_speed,new_heading,maximum,
                hex(read(mover+0x8c)[0]),hex(engine_vel[5]))
        world_expected=[float_multiply(w,0x42000000) for w in read(mover+0x80,2)]+[read(mover+0x8c)[0]]
        native_expected=read(mover+0x80,2)+[read(mover+0x8c)[0]]
        world_velocity_cases.append(dict(input=world_input,expected=world_expected,native_expected=native_expected))
        if engine:
            world_words=(ctypes.c_uint32*6)(*world_input)
            engine.pathing_velocity_world_commit(world_words)
            # ScalarMultiply canonicalizes zero; stored velocity rescaling preserves its sign.
            stored_expected=[w if w & 0x7fffffff == 0 else float_multiply(w,0x42000000) for w in native_expected[:2]]+[native_expected[2]]
            assert [world_words[0],world_words[1],world_words[5]]==stored_expected,(new_speed,new_heading,maximum,'world adapter')
            assert [float_multiply(w,0x3f800000) for w in stored_expected[:2]]+[stored_expected[2]]==world_expected
        integrated_old=(8+old_velocity[0]*.5,8+old_velocity[1]*.5)
        assert (scalar(mover+0x78),scalar(mover+0x7c))==integrated_old
        assert scalar(mover+0x70)==0.5 and read(mover+0x74)[0]==0
        actual=(scalar(mover+0x80),scalar(mover+0x84))
        desired=min(new_speed,maximum)
        expected=(desired*math.cos(scalar(heading_ptr)),desired*math.sin(scalar(heading_ptr)))
        error=max(abs(a-b) for a,b in zip(actual,expected))
        if not engine:assert error<0.00001,(new_speed,new_heading,maximum,actual,expected,error)
        assert bool(read(objects[1]+0x40)[0]&0x20000000)==bool(actual[0] or actual[1]),(new_speed,new_heading,maximum,actual,'moving occupancy flag')
        assert read(mover+0xc0)[0]==read(speed_ptr)[0]
        floats(clock+0x40,0.75)
        floats(displacement,0,0)
        engine_words=compare_integration(mover,clock,displacement)
        run(0x6f1603d0,mover,displacement)
        check_integration(mover,engine_words)
        wanted_position=tuple(integrated_old[n]+actual[n]*.25 for n in range(2))
        assert max(abs(scalar(mover+0x78+4*n)-wanted_position[n]) for n in [0,1])<0.000002
        assert scalar(mover+0x70)==0.75
        velocity_cases.append(dict(speed=new_speed,heading=scalar(heading_ptr),maximum=maximum,velocity=actual,facing_bits=read(mover+0x8c)[0],error=error))
    if args.world_velocity_fixture:
        args.world_velocity_fixture.write_text(json.dumps(dict(version=1,binary_sha256=digest,columns=['old_vx_world','old_vy_world','speed_world','heading','limit_world','old_facing'],expected_columns=['vx_world','vy_world','facing'],native_expected_columns=['vx_fine','vy_fine','facing'],cases=world_velocity_cases),separators=(',',':'))+'\n')
    # Two-member speed commit through shared-cap selection and actual movers.
    group,members=system+0xe000,system+0xf000
    actors=[system+0x10000,system+0x10200]
    actor_objects=[[system+0x10400,system+0x10500],[system+0x10600,system+0x10700]]
    auxiliary=system+0x10800
    sentinel=scalar(0x6fcd5480)
    cap_cases=0
    for count,excluded,cap_pair,previous,published in itertools.product(
            [0,1,2],[0,1,2,3],[(0.5,2),(2,0.5)],[0.25,1,sentinel],[0.25,1,sentinel]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x7c,auxiliary)
        floats(auxiliary+0x20,published,previous)
        for n,actor in enumerate(actors):
            write(members+n*0x2c+0x14,actor)
            write(members+n*0x2c+0x28,0x200000 if excluded&(1<<n) else 0)
            floats(actor+0x88,cap_pair[n])
        minimum=min([cap_pair[n] for n in range(count) if not excluded&(1<<n)]+[sentinel])
        run(0x6f16b060,group,speed_ptr)
        assert scalar(speed_ptr)==(minimum if published==sentinel else published)
        assert scalar(auxiliary+0x24)==min(previous,minimum)
        assert scalar(auxiliary+0x20)==published
        cap_cases+=1
    # Reference ownership and prior-pass speed publication shared by two groups.
    auxiliary2=system+0x10900
    request=system+0x10a00
    ownership_cases=0
    for entry,holder,offset in [(0x6f16d890,group,0x7c),(0x6f16d8b0,request,0xf8)]:
        for old,new,old_count,new_count in itertools.product(
                [0,auxiliary],[0,auxiliary,auxiliary2],[1,3],[1,5]):
            write(auxiliary+0x1c,old_count)
            write(auxiliary2+0x1c,new_count)
            write(holder+offset,old)
            expected={auxiliary:old_count,auxiliary2:new_count}
            if old: expected[old]-=1
            if new: expected[new]+=1
            run(entry,holder,new)
            assert read(holder+offset)[0]==new
            assert all(read(obj+0x1c)[0]==count for obj,count in expected.items())
            ownership_cases+=1
    publication_sequences=0
    other_group,other_members,other_actor=system+0x11000,system+0x11200,system+0x11400
    for first,second,reverse in itertools.product([0.25,0.5,1,4],[0.25,0.5,1,4],[False,True]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(other_group,bytes(0x100))
        write(auxiliary+0x1c,2)
        floats(auxiliary+0x20,sentinel,sentinel,4)
        for g,m,a in [(group,members,actors[0]),(other_group,other_members,other_actor)]:
            write(g+0x28,m)
            write(g+0x38,1)
            write(g+0x7c,auxiliary)
            write(m+0x14,a)
            write(m+0x28,0)
        previous=sentinel
        for maxima in [(first,second),(4,4),(second,first)]:
            floats(actors[0]+0x88,maxima[0])
            floats(other_actor+0x88,maxima[1])
            run(0x6f16c220,auxiliary)
            assert scalar(auxiliary+0x20)==previous
            assert scalar(auxiliary+0x24)==sentinel
            assert scalar(auxiliary+0x28)==0
            for g,maximum in ([(other_group,maxima[1]),(group,maxima[0])] if reverse else [(group,maxima[0]),(other_group,maxima[1])]):
                run(0x6f16b060,g,speed_ptr)
                assert scalar(speed_ptr)==(maximum if previous==sentinel else previous)
            previous=min(maxima)
            assert scalar(auxiliary+0x24)==previous
        publication_sequences+=1
    radius_cases=0
    radius_members,radius_actors=system+0x12000,system+0x13000
    for count,radius,override in itertools.product(range(10),[0.25,0.75,1.25,2],[None,0,0.5,4]):
        write(group+0x28,radius_members)
        write(group+0x38,count)
        write(group+0x7c,auxiliary if override is not None else 0)
        floats(auxiliary+0x28,0 if override is None else override)
        for n in range(count):
            actor=radius_actors+n*0x100
            write(radius_members+n*0x2c+0x14,actor)
            floats(actor+0x90,radius if n==count-1 else 0.125)
        run(0x6f16c940,group,speed_ptr)
        assert scalar(speed_ptr)==(override if override is not None else radius if count else 0)
        radius_cases+=1
    # Shared-radius pass resolves member identities and rejects stale ownership.
    radius_registry,radius_slots=system+0x15000,system+0x15100
    write(0x6fd68610,radius_registry)
    write(radius_registry+0xc,radius_slots)
    write(radius_registry+0x1c,4)
    radius_sequences=0
    for first,second,mode,reverse in itertools.product(
            [0.25,0.75,1.25,2],[0.25,0.75,1.25,2],['valid','other_group','stale_member','stale_group'],[False,True]):
        write(auxiliary+0x1c,2)
        floats(auxiliary+0x24,sentinel)
        run(0x6f16c220,auxiliary)
        for n,(g,m,a,r) in enumerate([(group,members,actors[0],first),(other_group,other_members,other_actor,second)]):
            write(g+0x18,222)
            write(g+0x28,m)
            write(g+0x38,1)
            write(g+0x7c,auxiliary)
            write(m,n,111+(n==1 and mode=='stale_member'))
            write(m+0x14,a)
            write(a+0x18,111)
            floats(a+0x90,r)
            write(a+0x9c,2+n if n==0 or mode!='other_group' else 2,222+(n==1 and mode=='stale_group'))
            write(radius_slots+n*8,-2,a)
            write(radius_slots+(n+2)*8,-2,g)
        for g in ([other_group,group] if reverse else [group,other_group]):
            run(0x6f16e1f0,g)
        expected=max(first,second) if mode=='valid' else first
        assert scalar(auxiliary+0x28)==expected,(first,second,mode,reverse,scalar(auxiliary+0x28))
        for g in [group,other_group]:
            run(0x6f16c940,g,speed_ptr)
            assert scalar(speed_ptr)==expected
        radius_sequences+=1
    # Original CPrClusterGroup constructor, zero-ref release and pool reuse.
    pool=owner+0x658
    previous_node,next_node,free_node=system+0x16100,system+0x16200,system+0x16000
    lifecycle_cases=0
    for previous,next,free_head,live in itertools.product(
            [0,previous_node],[0,next_node],[0,free_node],[1,3]):
        machine.mem_write(auxiliary-4,bytes(0x40))
        run(0x6f14fd70,auxiliary)
        assert read(auxiliary)[0]==0x6fa90c98
        assert read(auxiliary+0x14,3)==[0xffffffff,0xffffffff,0]
        assert (scalar(auxiliary+0x20),scalar(auxiliary+0x24),scalar(auxiliary+0x28))==(sentinel,sentinel,0)
        write(auxiliary+4,previous,next)
        if previous: write(previous+8,auxiliary)
        if next: write(next+4,auxiliary)
        floats(auxiliary+0x20,0.5,0.25,2)
        write(pool+0x14,free_head,live,77)
        run(0x6f16c220,auxiliary)
        assert read(auxiliary+4,2)==[0,0]
        if previous: assert read(previous+8)[0]==next
        if next: assert read(next+4)[0]==previous
        assert read(pool+0x14,3)==[auxiliary-4,live-1,77]
        assert read(auxiliary-4)[0]==free_head
        assert read(auxiliary+0x14,2)==[0xffffffff,0xffffffff]
        assert (scalar(auxiliary+0x20),scalar(auxiliary+0x24),scalar(auxiliary+0x28))==(sentinel,sentinel,0)
        run(0x6f155490,pool,0)
        assert machine.reg_read(UC_X86_REG_EAX)==auxiliary
        assert read(pool+0x14,3)==[free_head,live,77]
        assert (scalar(auxiliary+0x20),scalar(auxiliary+0x24),scalar(auxiliary+0x28))==(sentinel,sentinel,0)
        lifecycle_cases+=1
    registered_release_cases=0
    second_slots=system+0x15200
    for domain,index,generation,free_index,live in itertools.product(
            [0,0x80000000],[0,1,3],[1,123],[0xffffffff,2],[1,3]):
        machine.mem_write(radius_registry,bytes(0x80))
        write(radius_registry+0xc,radius_slots)
        write(radius_registry+0x2c,second_slots)
        write(radius_registry+0x1c,4)
        write(radius_registry+0x3c,4)
        write(radius_registry+0x40,free_index,free_index,live,live)
        for data in [radius_slots,second_slots]:
            machine.mem_write(data,bytes(32))
        data=second_slots if domain else radius_slots
        run(0x6f14fd70,auxiliary)
        identity=domain|index
        write(auxiliary+0x14,identity,generation)
        write(data+index*8,-2,auxiliary)
        write(pool+0x14,free_node,live,77)
        def resolve():
            machine.reg_write(UC_X86_REG_EDX,generation)
            run(0x6f054530,identity)
            return machine.reg_read(UC_X86_REG_EAX)
        assert resolve()==auxiliary
        other_before=bytes(machine.mem_read(radius_slots if domain else second_slots,32))
        run(0x6f16c220,auxiliary)
        assert resolve()==0
        assert read(data+index*8,2)==[free_index,0]
        assert read(radius_registry+0x40+(4 if domain else 0))[0]==index
        assert read(radius_registry+0x48+(4 if domain else 0))[0]==live-1
        assert read(radius_registry+0x40+(0 if domain else 4))[0]==free_index
        assert read(radius_registry+0x48+(0 if domain else 4))[0]==live
        assert bytes(machine.mem_read(radius_slots if domain else second_slots,32))==other_before
        assert read(auxiliary+0x14,2)==[0xffffffff,0xffffffff]
        assert read(pool+0x14,3)==[auxiliary-4,live-1,77]
        run(0x6f155490,pool,0)
        assert machine.reg_read(UC_X86_REG_EAX)==auxiliary
        assert resolve()==0  # reusing storage alone cannot revive the old handle
        registered_release_cases+=1
    # Full formation interval/rank pass, heading zero (ideal projection X; contact rounding checked separately).
    # Static initializer004140 parses this binary-owned decimal via070de0.
    # Initialize its scalar here; CRT isdigit import is not loaded in Unicorn.
    padding_text=bytes(machine.mem_read(0x6fa91bfc,32)).split(bytes([0]),1)[0]
    floats(0x6fd541d4,float(padding_text))
    formation_padding=scalar(0x6fd541d4)
    formation_cases=0
    formation_actors=[system+0x18000+n*0x100 for n in range(3)]
    for positions,ranks,radius,elapsed,velocity,skip in itertools.product(
            [(0,3,6),(6,3,0),(0,0,0),(0,0.5,1)],itertools.product(range(3),repeat=3),
            [0.25,1],[0,0.25],[0,1],[0,0x10000]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,3)
        floats(group+0x70,0)
        floats(owner+0x54,elapsed)
        write(owner+0x58,0)
        floats(owner+0x5c,8)
        flags=[0,skip,0]
        intervals=[]
        for n,actor in enumerate(formation_actors):
            machine.mem_write(actor,bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            write(members+n*0x2c+0x28,flags[n])
            floats(actor+0x78,positions[n],0,velocity if n==0 else 0,0)
            floats(actor+0x90,radius)
            write(actor+0xd8,ranks[n]<<12)
            center=positions[n]+(velocity if n==0 else 0)*elapsed
            intervals.append([n,center-radius-formation_padding,center+radius+formation_padding])
        # Selection sort: strictly smaller lower edge moves to the tail.
        for tail in range(2,0,-1):
            selected=tail
            for j in range(tail-1,-1,-1):
                if intervals[j][1]<intervals[selected][1]: selected=j
            intervals[tail],intervals[selected]=intervals[selected],intervals[tail]
        minimum_rank=16
        for at in range(2,-1,-1):
            n,left,right=intervals[at]
            if flags[n]&0x10000: continue
            if ranks[n]>minimum_rank:
                flags[n]|=0x200000
                continue
            minimum_rank=ranks[n]
            for j in range(at-1,-1,-1):
                peer,peer_left,_=intervals[j]
                if peer_left>=right: break
                if not flags[peer]&0x10000 and ranks[peer]<ranks[n]:
                    flags[n]|=0x200000
                    break
        run(0x6f16b2f0,group)
        actual=[read(members+n*0x2c+0x28)[0] for n in range(3)]
        assert actual==flags,(positions,ranks,radius,elapsed,velocity,skip,formation_padding,actual,flags)
        formation_cases+=1
    # Preserve a contact-boundary difference from the ideal arithmetic model.
    from unicorn import UC_HOOK_CODE
    formation_boundary=[]
    def capture_intervals(uc,address,size,data):
        pointer=read(uc.reg_read(UC_X86_REG_ESP)+4)[0]
        formation_boundary.extend([[scalar(pointer+0x34+n*8),scalar(pointer+0x38+n*8)] for n in range(3)])
    observer=machine.hook_add(UC_HOOK_CODE,capture_intervals,begin=0x6f16dfd0,end=0x6f16dfd0)
    machine.ctl_flush_tb()
    floats(owner+0x54,0)
    for n,actor in enumerate(formation_actors):
        floats(actor+0x78,n*2,0,0,0)
        floats(actor+0x90,1)
        write(actor+0xd8,0x1000 if n==0 else 0)
        write(members+n*0x2c+0x28,0x10000 if n==1 else 0)
    run(0x6f16b2f0,group)
    machine.hook_del(observer)
    assert len(formation_boundary)==3
    assert formation_boundary[2][0]<formation_boundary[0][1]
    assert [read(members+n*0x2c+0x28)[0] for n in range(3)]==[0x200000,0x10000,0]
    # Authored formation bridge: real handle resolution plus packed setter.
    formation_setter_cases=0
    formation_wrapper=system+0x19000
    write(0x6fd68610,radius_registry)
    write(radius_registry+0xc,radius_slots)
    write(radius_registry+0x1c,1)
    write(radius_slots,-2,actors[0])
    write(actors[0]+0x14,0,123)
    write(formation_wrapper+8,0,123)
    for value,prior in itertools.product(range(256),[0,0xffffffff,0x12345678,0x00010000]):
        write(actors[0]+0xd8,prior)
        run(0x6f05c3f0,formation_wrapper,value)
        assert read(actors[0]+0xd8)[0]==((prior&0xffff0fff)|(value<<12))
        formation_setter_cases+=1
    # Formation-marked members use the turn/arrival-only path, no route search.
    formation_step_cases=0
    formation_step_rows=[]
    held_path=system+0x1a000
    for moving,facing,arrival_radius,forced,prior_arrived in itertools.product([False,True],[0,0.5],[0.25,8],[0,0x10000],[0,0x10000]):
        actor=actors[0]
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        machine.mem_write(actor,bytes(0x100))
        machine.mem_write(held_path,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,1)
        write(members+0x14,actor)
        floats(members+0x18,12,8)
        write(members+0x28,0x200000|prior_arrived)
        write(actor+4,owner+0x200)
        floats(actor+0x78,8,8,0.25 if moving else 0,0,2,facing)
        floats(actor+0xb0,arrival_radius,0.25,0.125,0.25)
        write(actor+0xa8,held_path)
        write(actor+0xd8,forced)
        floats(owner+0x54,0)
        write(owner+0x58,0)
        run(0x6f16a790,group,members,0)
        initial_heading=0 if moving else facing
        arrived=bool((arrival_radius>=4 or forced) and initial_heading<=0.2)
        assert scalar(members+0x20)==0
        assert abs(scalar(members+0x24)-(initial_heading if arrived else max(0,initial_heading-0.125)))<0.00001
        assert bool(read(members+0x28)[0]&0x10000)==arrived
        assert read(members+0x28)[0]&0x200000
        assert (scalar(actor+0x78),scalar(actor+0x7c))==(8,8)
        assert (scalar(actor+0x80),scalar(actor+0x84))==(0.25 if moving else 0,0)
        if arrived: assert not read(actor+0xd8)[0]&0x10000
        formation_step_rows.append(dict(moving=moving,facing=float_bits(facing),range=float_bits(arrival_radius),forced=forced,prior=prior_arrived,flags=read(members+0x28)[0],speed=read(members+0x20)[0],heading=read(members+0x24)[0]))
        formation_step_cases+=1
    # Layout prepass groups predicted positions by authored formation rank.
    layout_buckets=system+0x1b000
    layout_bucket_cases=0
    layout_error=0
    for ranks,heading,elapsed,domain in itertools.product(
            itertools.product([0,2,4],repeat=3),[0,0.5,pi/2],[0,0.5],[0,0x80000000]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        machine.mem_write(layout_buckets,bytes(16*0xa4))
        write(group+0x28,members)
        write(group+0x38,3)
        floats(group+0x70,heading)
        clock=owner+(0x68 if domain else 0x14)
        floats(clock+0x40,elapsed)
        write(clock+0x44,0)
        expected={}
        for n,actor in enumerate(formation_actors):
            machine.mem_write(actor,bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            write(actor+0x14,domain)
            floats(actor+0x78,2*n,4-n,0.25,-0.125)
            write(actor+0xd8,ranks[n]<<12)
            x,y=2*n+0.25*elapsed,4-n-0.125*elapsed
            angle=scalar(group+0x70)
            expected.setdefault(ranks[n],[]).append((n,x*math.cos(angle)+y*math.sin(angle),-x*math.sin(angle)+y*math.cos(angle)))
        run(0x6f16cb80,group,layout_buckets)
        assert machine.reg_read(UC_X86_REG_EAX)==len(expected)
        for rank in range(16):
            rows=expected.get(rank,[])
            bucket_ptr=layout_buckets+rank*0xa4
            assert read(bucket_ptr)[0]==len(rows)
            for j,(index,x,y) in enumerate(rows):
                assert read(bucket_ptr+4+j*4)[0]==index
                error=max(abs(scalar(bucket_ptr+0x34+j*8)-x),abs(scalar(bucket_ptr+0x38+j*8)-y))
                assert error<0.0001,(ranks,heading,elapsed,domain,j,error)
                layout_error=max(layout_error,error)
        layout_bucket_cases+=1
    # Row-dimension selector: original radius scan, table choice and arithmetic.
    row_tables=[read(0x6fa91d20,26),read(0x6fa91d88,26)]
    row_gap=float(bytes(machine.mem_read(0x6fa91eac,32)).split(bytes([0]),1)[0])
    floats(0x6fd541c0,row_gap)
    floats(0x6fd541cc,row_gap)
    row_dimension_cases=0
    row_actors=[system+0x1c000+n*0x100 for n in range(12)]
    for count,radius,rank_count,group_flags,prior in itertools.product(
            range(1,13),[0.25,0.5,1.5,2],[1,2],[0,0x20],[0,100]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x400))
        machine.mem_write(layout_buckets,bytes(16*0xa4))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x80,group_flags)
        write(layout_buckets,count)
        for n,actor in enumerate(row_actors[:count]):
            write(members+n*0x2c+0x14,actor)
            floats(actor+0x90,radius if n==count-1 else radius/2)
            write(layout_buckets+4+n*4,n)
        if rank_count==2: write(layout_buckets+0xa4,1,0)
        floats(speed_ptr,prior)
        run(0x6f16d270,group,layout_buckets,speed_ptr,rank_count)
        widths=[]
        for b,n,r in [(0,count,radius)]+([(1,1,radius if count==1 else radius/2)] if rank_count==2 else []):
            cap=min(n,row_tables[rank_count!=1][n+13*int(r>=1.5)])
            diameter=2*r
            width=diameter*cap+(diameter+row_gap)*(cap-1)
            bucket_ptr=layout_buckets+b*0xa4
            assert scalar(bucket_ptr+0x94)==diameter
            assert scalar(bucket_ptr+0x98)==width,(count,radius,rank_count,group_flags,cap,row_gap,scalar(bucket_ptr+0x98),width)
            assert read(bucket_ptr+0x9c)[0]==cap
            assert scalar(bucket_ptr+0xa0)==cap
            widths.append(width)
        assert scalar(speed_ptr)==max(prior,*widths)
        row_dimension_cases+=1
    row_placement_cases=0
    row_x,row_width,row_diameter,row_cursor=[system+0x1d000+n for n in [0,4,8,12]]
    for count,requested,width,diameter in itertools.product(range(1,13),[1,2,3,4],[4,8],[1,2]):
        for start in ([0,2] if count>2 else [0]):
            machine.mem_write(members,bytes(0x400))
            machine.mem_write(layout_buckets,bytes(0xa4))
            write(group+0x28,members)
            write(layout_buckets,count)
            rows=[[n,100+n,(n*7)%5] for n in range(count)]
            for n,x,y in rows:
                write(layout_buckets+4+n*4,n)
                floats(layout_buckets+0x34+n*8,x,y)
                floats(members+n*0x2c+0xc,-99,-99)
            number=min(requested,count-start)
            for tail in range(start+number-1,start,-1):
                selected=tail
                for j in range(tail-1,start-1,-1):
                    if rows[selected][2]<rows[j][2]: selected=j
                rows[tail],rows[selected]=rows[selected],rows[tail]
            floats(row_x,3,width,diameter)
            write(row_cursor,start)
            run(0x6f16cf40,group,layout_buckets,row_x,row_width,row_diameter,requested,row_cursor)
            assert machine.reg_read(UC_X86_REG_EAX)==int(start+number<count)
            assert read(row_cursor)[0]==start+number
            assert read(layout_buckets+4,count)==[row[0] for row in rows]
            chosen={row[0]:j for j,row in enumerate(rows[start:start+number])}
            for n in range(count):
                actual=(scalar(members+n*0x2c+0xc),scalar(members+n*0x2c+0x10))
                if n in chosen:
                    expected_y=width/2 if number==1 else diameter/2+chosen[n]*(width-diameter)/(number-1)
                    assert actual[0]==3 and abs(actual[1]-expected_y)<0.00001
                else: assert actual==(-99,-99)
            row_placement_cases+=1
    center_rotate_cases=0
    center_rotate_error=0
    for count,heading,translation,pattern in itertools.product(range(1,13),[0,0.5,pi/2,pi],[0,8],[0,1]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x400))
        write(group+0x28,members)
        write(group+0x38,count)
        floats(group+0x70,heading)
        points=[(translation+n*0.25,translation+(n%3)*0.5 if pattern else translation) for n in range(count)]
        mean=[sum(p[k] for p in points)/count for k in [0,1]]
        for n,point in enumerate(points):
            floats(members+n*0x2c+0xc,*point)
            write(members+n*0x2c+0x28,0x12345678)
        run(0x6f169e10,group)
        angle=scalar(group+0x70)
        for n,(x,y) in enumerate(points):
            x-=mean[0];y-=mean[1]
            expected=(x*math.cos(angle)-y*math.sin(angle),x*math.sin(angle)+y*math.cos(angle))
            actual=(scalar(members+n*0x2c+0xc),scalar(members+n*0x2c+0x10))
            error=max(abs(a-b) for a,b in zip(actual,expected))
            assert error<0.0001,(count,heading,translation,pattern,n,actual,expected,error)
            assert read(members+n*0x2c+0x28)[0]==0x12345678
            center_rotate_error=max(center_rotate_error,error)
        center_rotate_cases+=1
    rank_gap=float(bytes(machine.mem_read(0x6fa91ea8,32)).split(bytes([0]),1)[0])
    depth_gap=float(bytes(machine.mem_read(0x6fa91eb0,32)).split(bytes([0]),1)[0])
    for address in [0x6fd541bc,0x6fd541c8]: floats(address,rank_gap)
    for address in [0x6fd541c4,0x6fd541d0]: floats(address,depth_gap)
    formation_raw_cases=[]
    def exact_formation(name):
        count=read(group+0x38)[0]
        inputs=[count,read(group+0x70)[0]]
        for n in range(count):
            actor=read(members+n*0x2c+0x14)[0]
            clock=owner+(0x68 if read(actor+0x14)[0]&0x80000000 else 0x14)
            inputs += read(actor+0x78,4)+read(actor+0x70,2)+read(clock+0x40,3)+read(actor+0x90)+[(read(actor+0xd8)[0]>>12)&15]
        run(0x6f16a5b0,group)
        outputs=[1]
        for n in range(count):outputs += read(members+n*0x2c+0xc,2)
        if engine:
            engine.pathing_formation.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
            actual=(ctypes.c_uint32*len(outputs))()
            engine.pathing_formation((ctypes.c_uint32*len(inputs))(*inputs),actual)
            assert list(actual)==outputs,(name,inputs,list(actual),outputs)
        formation_raw_cases.append(dict(name=name,flags=read(group+0x80)[0],input=inputs,output=outputs))
    full_layout_cases=0
    full_layout_error=0
    for count,radius,pattern,group_flags in itertools.product(range(1,13),[0.25,1.5],range(3),[0,0x20]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x400))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x80,group_flags)
        buckets={}
        for n,actor in enumerate(row_actors[:count]):
            machine.mem_write(actor,bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            rank=0 if pattern==0 else (n%(pattern+1))*2
            write(actor+0xd8,rank<<12)
            floats(actor+0x78,n*0.25,(n%3)*0.5,0,0)
            floats(actor+0x90,radius)
            buckets.setdefault(rank,[]).append(n)
        diam=2*radius
        capacities={rank:min(len(ids),row_tables[len(buckets)!=1][len(ids)+13*int(radius>=1.5)]) for rank,ids in buckets.items()}
        width=max(diam*k+(diam+row_gap)*(k-1) for k in capacities.values())
        expected=[None]*count
        row_position=0
        for rank,ids in sorted(buckets.items()):
            ids=sorted(ids,reverse=True)  # unique increasing projected X
            cap=capacities[rank]
            for start in range(0,len(ids),cap):
                span=ids[start:start+cap]
                for tail in range(len(span)-1,0,-1):
                    selected=tail
                    for j in range(tail-1,-1,-1):
                        if span[selected]%3<span[j]%3: selected=j
                    span[tail],span[selected]=span[selected],span[tail]
                for j,n in enumerate(span):
                    expected[n]=(row_position,width/2 if len(span)==1 else diam/2+j*(width-diam)/(len(span)-1))
                if start+cap<len(ids): row_position-=diam+depth_gap
            row_position-=rank_gap
        mean=[sum(p[k] for p in expected)/count for k in [0,1]]
        exact_formation("uniform-%d-%s-%d-%d"%(count,radius,pattern,group_flags))
        for n,point in enumerate(expected):
            actual=[scalar(members+n*0x2c+0xc+k*4) for k in [0,1]]
            error=max(abs(actual[k]-(point[k]-mean[k])) for k in [0,1])
            assert error<0.0001,(count,radius,pattern,group_flags,n,actual,point,mean,error,rank_gap,depth_gap)
            full_layout_error=max(full_layout_error,error)
        full_layout_cases+=1
    mixed_formation_cases=0
    for count,pattern,heading,shape,elapsed,flags in itertools.product([2,3,5,9,12],range(3),[0,.5,pi/2,3.1],range(3),[0,.125],[0,0x20]):
        machine.mem_write(group,bytes(0x100));machine.mem_write(members,bytes(0x400))
        write(group+0x28,members);write(group+0x38,count);write(group+0x80,flags);floats(group+0x70,heading)
        for domain in (0,0x80000000):
            clock=owner+(0x68 if domain else 0x14)
            floats(clock+0x40,.25);write(clock+0x44,1);floats(clock+0x48,.5)
        for n,actor in enumerate(row_actors[:count]):
            machine.mem_write(actor,bytes(0x100));write(members+n*0x2c+0x14,actor)
            rank=0 if pattern==0 else n%(pattern+1)*3
            write(actor+0xd8,rank<<12);write(actor+0x14,0x80000000 if n&1 else 0)
            floats(actor+0x70,.25-elapsed-(n%3)*.03125);write(actor+0x74,0 if n&1 else 1)
            position=(n*.25,(n%3)*.5) if shape==0 else ((8,8) if shape==1 else ((n%2)*.125,-n*.03125))
            velocity=(0,0) if shape==1 else ((n%3)*.125-.125,.25)
            floats(actor+0x78,*position,*velocity);floats(actor+0x90,[.25,.5,1.5,2][n%4])
        exact_formation("mixed-%d-%d-%s-%d-%s-%d"%(count,pattern,heading,shape,elapsed,flags))
        mixed_formation_cases+=1
    # Actual engine selection witness: two front-rank Footmen and one rear,
    # facing east, radius16 world units, exact32-unit fine coordinates.
    machine.mem_write(group,bytes(0x100));machine.mem_write(members,bytes(0x400))
    write(group+0x28,members);write(group+0x38,3)
    floats(owner+0x54,0);write(owner+0x58,0);floats(owner+0x5c,.5)
    for n,actor in enumerate(row_actors[:3]):
        machine.mem_write(actor,bytes(0x100));write(members+n*0x2c+0x14,actor)
        floats(actor+0x78,44,57.75+n*6.25,0,0);floats(actor+0x90,.5);write(actor+0xd8,(1 if n==2 else 0)<<12)
    exact_formation('engine-order-rank-witness')
    if args.formation_fixture:args.formation_fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=formation_raw_cases,scope='Complete original layout; uniform baseline plus mixed radii, oblique headings, ties, velocity prediction and both clock domains; count<=12'),separators=(',',':'))+'\n')
    formation_refresh_cases=0
    refresh_destination=system+0x1d100
    for count,delta,prior_heading,special in itertools.product([1,3,6],[(0,0),(4,0),(0,4),(-4,0)],[0,0.5],[0,0x200]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x400))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x80,special|0x40)
        floats(group+0x54,8,8)
        floats(group+0x70,prior_heading)
        floats(refresh_destination,8+delta[0],8+delta[1])
        for n,actor in enumerate(row_actors[:count]):
            machine.mem_write(actor,bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0xc,17+n,19+n)
            floats(actor+0x78,n,2*n,0,0)
            floats(actor+0x90,0.25)
            write(actor+0xd8,(n%2)<<12)
        before=bytes(machine.mem_read(members,count*0x2c))
        run(0x6f16d990,group,refresh_destination)
        assert (scalar(group+0x54),scalar(group+0x58))==(8+delta[0],8+delta[1])
        wanted_heading=prior_heading if delta==(0,0) else math.atan2(delta[1],delta[0])
        assert abs(scalar(group+0x70)-wanted_heading)<0.0003
        assert read(group+0x80)[0]==special|0x10040
        actual=bytes(machine.mem_read(members,count*0x2c))
        machine.mem_write(members,before)
        if not special: run(0x6f16a5b0,group)
        assert bytes(machine.mem_read(members,count*0x2c))==actual
        formation_refresh_cases+=1
    regroup_thresholds=[]
    for dest,text_addr in [(0x6fd541a4,0x6fa91e84),(0x6fd541a8,0x6fa91e8c)]:
        value=float(bytes(machine.mem_read(text_addr,32)).split(bytes([0]),1)[0])
        floats(dest,value)
        regroup_thresholds.append(scalar(dest))
    regroup_cases=0
    arrived_out,near_out=system+0x1d200,system+0x1d204
    for distance,arrived_mask,flags,cooldown,mover_flag,elapsed in itertools.product(
            [0,1,2,4,8,16,32],range(8),[0,4,0x100,0x104],[0,1],[0,0x1000000],[0,0.5]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,3)
        write(group+0x80,flags)
        write(group+0x68,cooldown)
        floats(owner+0x54,elapsed)
        write(owner+0x58,0)
        arrived=near=0
        for n,actor in enumerate(formation_actors):
            machine.mem_write(actor,bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,distance+n,0)
            write(members+n*0x2c+0x28,0x10000 if arrived_mask&(1<<n) else 0)
            floats(actor+0x78,0,0,4 if n==0 else 0,0)
            write(actor+0xd8,mover_flag if n==2 else 0)
            if arrived_mask&(1<<n): arrived+=1
            elif (distance+n-(4*elapsed if n==0 else 0))**2<regroup_thresholds[bool(flags&0x100)]: near+=1
        run(0x6f16b120,group,arrived_out,near_out)
        expected=(0 if cooldown or mover_flag or flags&4 else 3-arrived-near) if arrived else 3
        assert machine.reg_read(UC_X86_REG_EAX)==expected,(distance,arrived_mask,flags,cooldown,mover_flag,elapsed,regroup_thresholds)
        assert read(arrived_out)[0]==arrived
        assert read(near_out)[0]==near
        regroup_cases+=1
    regroup_advance_cases=0
    regroup_advance_rows=[]
    regroup_paths=[system+0x1e000,system+0x1e100]
    shared_path,shared_points=system+0x1e200,system+0x1e400
    run(0x6f0040d0,0)
    for flags,counter,arrived_mask,distance,cooldown,index in itertools.product(
            [0,0x100,0x20100],[99,100,198,199,396,397],[0,1,3],[0,32],[0,1],[0,2,3]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        machine.mem_write(shared_path,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,2,shared_path)
        floats(group+0x54,8,8)
        write(group+0x5c,22,counter)
        write(group+0x68,cooldown)
        write(group+0x80,flags)
        write(shared_path+0x60,shared_points)
        write(shared_path+0x70,3)
        write(shared_path+0x78,index)
        floats(shared_path+0x1c,12,8)
        floats(shared_points,6,4,5,4,4,4)
        floats(owner+0x54,0)
        write(owner+0x58,0)
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            machine.mem_write(regroup_paths[n],bytes(0x100))
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,distance,0)
            write(members+n*0x2c+0x28,0x10000 if arrived_mask&(1<<n) else 0)
            floats(actor+0x78,n*0.25,0,0,0)
            floats(actor+0x90,0.25)
            write(actor+0xa8,regroup_paths[n])
            write(regroup_paths[n]+0x50,5)
            write(regroup_paths[n]+0x70,5,3,4)
            write(regroup_paths[n]+0x88,0x30100000)
            write(regroup_paths[n]+0x94,17,29)
        arrived=arrived_mask.bit_count()
        near=(2-arrived) if distance==0 else 0
        status=(0 if cooldown else 2-arrived-near) if arrived else 2
        limit=99 if not flags&0x100 else 396 if flags&0x20000 else 198
        condition=status==0 or counter>limit
        advance=condition and index==2
        run(0x6f16c4f0,group)
        assert read(group+0x60)[0]==(0 if advance else counter+int(not condition and arrived>0))
        assert read(group+0x5c)[0]==(0 if advance else 22)
        assert read(shared_path+0x78)[0]==(0 if advance else index)
        assert read(group+0x80)[0]==((flags|0x10000)&~0x20000 if advance else flags)
        assert (scalar(group+0x54),scalar(group+0x58))==((12,8) if advance else (8,8))
        for n,path_ptr in enumerate(regroup_paths):
            assert read(path_ptr+0x50)[0]==(0 if advance else 5)
            assert read(path_ptr+0x70,3)==([0,0xffffffff,0xffffffff] if advance else [5,3,4])
            assert read(path_ptr+0x94,2)==([0,0] if advance else [17,29])
            assert read(path_ptr+0x88)[0]==(0 if advance else 0x30100000)
            assert read(members+n*0x2c+0x28)[0]==(0 if advance else 0x10000 if arrived_mask&(1<<n) else 0)
            if advance:
                assert abs(scalar(members+n*0x2c+0xc))<0.00001
                assert abs(scalar(members+n*0x2c+0x10)-(1.5 if n==0 else -1.5))<0.00001
        regroup_advance_rows.append(dict(input=[flags,counter,arrived_mask,distance,cooldown,index],output=[read(group+0x80)[0],read(group+0x5c)[0],read(group+0x60)[0],read(shared_path+0x78)[0]],member_flags=[read(members+n*0x2c+0x28)[0]for n in range(2)],paths=[read(p+0x50)+read(p+0x70,3)+read(p+0x94,2)for p in regroup_paths]))
        regroup_advance_cases+=1
    if args.formation_refresh_fixture:
        args.formation_refresh_fixture.write_text(json.dumps(dict(binary_sha256=digest,passed=True,member_cases=formation_step_rows,regroup_cases=regroup_advance_rows,refresh_cases=formation_refresh_cases,regroup_status_cases=regroup_cases,scope="Complete original16a790 held decisions with prior arrival bit; complete16c4f0 through reset/advance/layout, no code replacement"),separators=(',',':'))+'\n')
        args.report.write_text(json.dumps(dict(binary_sha256=digest,passed=True,member_cases=formation_step_cases,regroup_advance_cases=regroup_advance_cases,refresh_cases=formation_refresh_cases,regroup_status_cases=regroup_cases))+'\n')
        return
    decision_cases=0
    decision_observations=[]
    def observe_member_step(uc,address,size,data):
        decision_observations.append([read(obj_pair[1]+0x40)[0] for obj_pair in actor_objects])
    decision_hook=machine.hook_add(UC_HOOK_CODE,observe_member_step,begin=0x6f16a790,end=0x6f16a790)
    machine.ctl_flush_tb()
    for flags,age,cooldown,rank_pair,prior_flags,count,occupancy in itertools.product(
            [0,0x20000,0x20002,0x20200],[0,65,66],[0,1],[(0,0),(1,0)],
            [0,0x100000,0x200000],[1,2],[0,0x20000000]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x80,flags)
        write(group+0x5c,age)
        write(group+0x68,cooldown)
        floats(group+0x74,sentinel)
        floats(owner+0x54,0)
        write(owner+0x58,0)
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            machine.mem_write(regroup_paths[n],bytes(0x100))
            write(actor+4,owner+0x200)
            floats(actor+0x78,8,8,0,0,2,0)
            floats(actor+0x90,0.25)
            floats(actor+0xb0,0.25,0.25,0.125,0.25)
            write(actor+0x94,*actor_objects[n])
            write(actor+0xa8,regroup_paths[n])
            write(actor+0xd8,(rank_pair[n]<<12)|0x10000)
            write(actor_objects[n][1]+0x40,occupancy)
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,12,8)
            write(members+n*0x2c+0x28,prior_flags)
        expected_flags=[prior_flags&~0x200000]*count
        if count>1 and not flags&0x200:
            if not cooldown and not flags&2 and rank_pair==(1,0): expected_flags[0]|=0x200000
            if not flags&0x20000 or age>65: expected_flags=[f|0x100000 for f in expected_flags]
        exemptions=[occupancy|(0x40000000 if count>1 and not flags&0x200 and not f&0x300000 else 0) for f in expected_flags]
        if count==1: exemptions.append(occupancy)
        decision_observations.clear()
        run(0x6f16c250,group,0)
        assert decision_observations==[exemptions]*count,(flags,age,cooldown,rank_pair,prior_flags,count,occupancy,decision_observations,exemptions)
        for n,actor in enumerate(actors[:count]):
            assert read(actor_objects[n][1]+0x40)[0]==occupancy
            assert read(members+n*0x2c+0x28)[0]==expected_flags[n]|0x10000
            assert scalar(members+n*0x2c+0x20)==0
            assert not read(actor+0xd8)[0]&0x10000
        if count==1:
            assert read(members+0x2c+0x28)[0]==prior_flags
            assert read(actors[1]+0xd8)[0]==(rank_pair[1]<<12)|0x10000
        decision_cases+=1
    decision_hold_cases=0
    for moving,facing,occupancy in itertools.product([0,1],[0,0.5,1],[0,0x20000000]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,2)
        write(group+0x80,0x20000)
        floats(group+0x74,sentinel)
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            machine.mem_write(regroup_paths[n],bytes(0x100))
            write(actor+4,owner+0x200)
            floats(actor+0x78,8,8,0.25 if moving and n==0 else 0,0,2,facing if n==0 else 0)
            floats(actor+0x90,0.25)
            floats(actor+0xb0,0.25,0.25,0.125,0.25)
            write(actor+0x94,*actor_objects[n])
            write(actor+0xa8,regroup_paths[n])
            write(actor+0xd8,0x1000 if n==0 else 0x10000)
            write(actor_objects[n][1]+0x40,occupancy)
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,12,8)
        decision_observations.clear()
        run(0x6f16c250,group,0)
        assert decision_observations==[[occupancy,occupancy|0x40000000]]*2
        assert read(members+0x28)[0]==0x200000
        assert read(members+0x2c+0x28)[0]==0x10000
        assert scalar(members+0x20)==0
        assert abs(scalar(members+0x24)-max(0,(0 if moving else facing)-0.125))<0.00001
        for n,actor in enumerate(actors):
            assert read(actor_objects[n][1]+0x40)[0]==occupancy
            assert (scalar(actor+0x78),scalar(actor+0x7c))==(8,8)
            assert (scalar(actor+0x80),scalar(actor+0x84))==(0.25 if moving and n==0 else 0,0)
        decision_hold_cases+=1
    # Ordinary non-arrival decisions, cached routes, then actual group commit.
    machine.mem_map(0,0x1000)  # Original blocker collector saves/restores FS:[0].
    machine.mem_map(0x10100000,0x10000)
    fine_system,acc_system,acc_map,candidate_data=0x10100000,0x10101000,0x10102000,0x10103000
    write(owner+0x24c,fine_system,acc_system)
    write(owner+0x238,maps[1])
    write(fine_system+0x1c,maps[1])
    write(fine_system+0xb8,candidate_data)
    write(fine_system+0xc4,64,0)
    write(acc_system+0x1c,acc_map)
    floats(acc_map+0x64,2,0.5)
    decision_route_cases=tick_cases=0
    region_observations=[]
    def observe_region_callback(uc,address,size,data):
        region_observations.append(uc.reg_read(UC_X86_REG_ECX))
    region_hook=machine.hook_add(UC_HOOK_CODE,observe_region_callback,begin=0x6f16fa00,end=0x6f16fa00)
    machine.ctl_flush_tb()
    commit_observations=[]
    def observe_group_commit(uc,address,size,data):
        commit_observations.append([(read(pair[1]+0x40)[0],scalar(actor+0x80),scalar(actor+0x84)) for actor,pair in zip(actors,actor_objects)])
    commit_hook=machine.hook_add(UC_HOOK_CODE,observe_group_commit,begin=0x6f16c570,end=0x6f16c570)
    machine.ctl_flush_tb()
    for old_speed,cls,caps,flags,ranks,whole_tick in itertools.product(
            [0,0.25,0.5],range(4),[(0.5,1),(2,1)],[0,0x20000,0x20002],[(0,0),(1,0)],[False,True]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,2)
        write(group+0x80,flags)
        floats(group+0x74,sentinel)
        floats(owner+0x54,0)
        write(owner+0x58,0)
        for grid,data,bitmap in zip(maps,cells,bitmaps):
            write(grid+0x88,0)
            write(grid+0xac,0xffffff,0)
            machine.mem_write(bitmap,bytes(32))
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            write(actor,0x6fa9129c,owner+0x200,0)
            floats(actor+0x78,8,6+4*n,old_speed,0,caps[n],0)
            floats(actor+0x90,0.25+0.5*cls)
            write(actor+0xd8,ranks[n]<<12)
            floats(actor+0xb0,0.25,0.25,0.125,0.25)
            write(actor+0x94,*actor_objects[n])
            path_ptr=regroup_paths[n]
            write(actor+0xa8,path_ptr)
            machine.mem_write(path_ptr,bytes(0x100))
            floats(path_ptr+0x1c,12,6+4*n,12,6+4*n)
            fine_data=0x10104000+n*0x100
            coarse_data=0x10105000+n*0x100
            floats(fine_data,12,6+4*n)
            floats(coarse_data,6,3+2*n)
            write(path_ptr+0x40,fine_data)
            write(path_ptr+0x4c,16,1)
            write(path_ptr+0x60,coarse_data)
            write(path_ptr+0x6c,16,1,0,0)
            write(path_ptr+0x84,700|400<<16)
            write(path_ptr+0x88,0x200000)
            write(path_ptr+0x9c,0x02000000)
            write(path_ptr+0xa8,-1,-1)
            floats(path_ptr+0xb4,0.25+0.5*cls)
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,12,6+4*n)
            for obj,grid in zip(actor_objects[n],maps):
                machine.mem_write(obj,bytes(0x80))
                write(obj+0x2c,grid)
                write(obj+0x34,0x01000001)
                write(obj+0x40,0x20000000 if old_speed else 0)
        if whole_tick:
            tick_registry,tick_slots,tick_path=0x10106000,0x10106100,0x10106200
            machine.mem_write(tick_registry,bytes(0x100))
            machine.mem_write(tick_path,bytes(0x100))
            write(0x6fd68610,tick_registry)
            write(tick_registry+0xc,tick_slots)
            write(tick_registry+0x1c,3)
            for n,item in enumerate(actors+[group]):
                write(tick_slots+n*8,-2,item)
                write(item+0x14,n,100+n)
            for n,actor in enumerate(actors):
                write(actor+0x9c,2,102)
                write(actor+0xd0,8,6+4*n)
                write(members+n*0x2c,n,100+n)
            write(group+0x3c,tick_path)
            write(group+0x40,-1,-1)
            floats(group+0x4c,12,8)
            floats(tick_path+0x1c,12,8,12,8)
            write(tick_path+0x70,1,0,0)
        decision_observations.clear()
        commit_observations.clear()
        region_observations.clear()
        if whole_tick:
            run(0x6f16c150,group)
            assert len(commit_observations)==1
            assert region_observations==list(reversed(actors))
            assert read(group+0x38)[0]==2
            assert read(group+0x5c,2)==[1,1]
            assert read(group+0x80)[0]==flags
        else:
            run(0x6f16c250,group,0)
            observe_group_commit(machine,0,0,None)
        occupancy=0x20000000 if old_speed else 0
        held=ranks==(1,0) and not flags&2
        exempt=[bool(flags&0x20000 and not (held and n==0)) for n in range(2)]
        assert decision_observations==[[occupancy|(0x40000000 if enabled else 0) for enabled in exempt]]*2
        expected_speeds=[0 if held and n==0 else min(caps[n],old_speed+0.25) for n in range(2)]
        for n,actor in enumerate(actors):
            assert commit_observations[0][n][0]==occupancy
            expected_flags=(0x200000 if held and n==0 else 0)|(0x100000 if not flags&0x20000 or expected_speeds[n]>0 else 0)
            assert read(members+n*0x2c+0x28)[0]==expected_flags
            assert abs(scalar(members+n*0x2c+0x20)-expected_speeds[n])<0.00001
            assert abs(scalar(members+n*0x2c+0x24))<0.00001
            assert commit_observations[0][n][1:]==(old_speed,0)
            assert read(regroup_paths[n]+0x74,2)==[0,0]
        if not whole_tick: run(0x6f16c570,group,vector)
        committed_speeds=[speed if held else min(speed,min(caps)) for speed in expected_speeds]
        for n,actor in enumerate(actors):
            assert abs(scalar(actor+0x80)-committed_speeds[n])<0.00001
            assert abs(scalar(actor+0x84))<0.00001
            assert (scalar(actor+0x78),scalar(actor+0x7c))==(8,6+4*n)
            assert read(actor_objects[n][1]+0x40)[0]==(0x20000000 if committed_speeds[n] else 0)
        floats(owner+0x54,0.25)
        floats(displacement,0,0)
        for n,actor in enumerate(actors):
            run(0x6f1603d0,actor,displacement)
            assert abs(scalar(actor+0x78)-(8+committed_speeds[n]*0.25))<0.000002
            assert abs(scalar(actor+0x7c)-(6+4*n))<0.000002
        assert read(0)[0]==0
        if whole_tick: tick_cases+=1
        else: decision_route_cases+=1
    machine.hook_del(commit_hook)
    machine.hook_del(decision_hook)
    # Membership prepass: original handle resolution, region callback, swap removal.
    prepass_cases=0
    for count in range(4):
        for states in itertools.product(['valid','stale_member','deleted','wrong_group','stale_group'],repeat=count):
            machine.mem_write(group,bytes(0x100))
            machine.mem_write(other_group,bytes(0x100))
            machine.mem_write(members,bytes(0x100))
            machine.mem_write(tick_registry,bytes(0x100))
            write(0x6fd68610,tick_registry)
            write(tick_registry+0xc,tick_slots)
            write(tick_registry+0x1c,5)
            write(group+0x28,members)
            write(group+0x38,count)
            for n,item in enumerate(formation_actors+[group,other_group]):
                write(tick_slots+n*8,-2,item)
                write(item+0x14,n,100+n)
            floats(owner+0x54,0)
            write(owner+0x58,0)
            rows=[]
            for n,state in enumerate(states):
                actor=formation_actors[n]
                machine.mem_write(actor,bytes(0x100))
                write(actor,0x6fa9129c,owner+0x200,0)
                write(actor+0x14,n,100+n)
                floats(actor+0x78,8,6+n)
                write(actor+0xa8,regroup_paths[0])
                write(actor+0xd0,8,6+n)
                write(actor+0x9c,4 if state=='wrong_group' else 3,104 if state=='wrong_group' else 999 if state=='stale_group' else 103)
                if state=='deleted': write(tick_slots+n*8+4,0)
                row=[n,999 if state=='stale_member' else 100+n]+[0x12340000+n*16+k for k in range(2,11)]
                write(members+n*0x2c,*row)
                row[5]=actor
                rows.append(row)
            expected=list(rows)
            for n in reversed(range(count)):
                if states[n]!='valid':
                    expected[n]=expected[-1]
                    expected.pop()
            region_observations.clear()
            run(0x6f16bc10,group)
            assert machine.reg_read(UC_X86_REG_EAX)==len(expected),(states,'return')
            assert read(group+0x38)[0]==len(expected)
            assert region_observations==[formation_actors[n] for n in reversed(range(count)) if states[n]=='valid'],states
            for n,row in enumerate(expected):
                assert read(members+n*0x2c,11)==row,(states,n,read(members+n*0x2c,11),row)
            prepass_cases+=1
    machine.hook_del(region_hook)
    # A controlled request at an actual slot54 callback boundary, followed by
    # the unmodified callback and prepass. Pause the VM, execute the original
    # producer on a separate stack, then restore CPU context (never memory).
    # This tests callback timing; the gameplay/JASS producer graph is excluded.
    callback_mutations=[]
    callback_stack=0x2000e000
    from wc3_pathing_callbacks import invoke_preserving_context
    def call_during_callback(entry,receiver,arguments):
        invoke_preserving_context(machine,dict(entry=entry,receiver=receiver,arguments=arguments,
                                  stack=callback_stack,stop=stop))
    for count in range(1,4):
        for trigger,removed,action in itertools.product(range(count),range(1<<count),['unbind_member','detach_mover']):
            machine.mem_write(group,bytes(0x100));machine.mem_write(members,bytes(0x100))
            machine.mem_write(tick_registry,bytes(0x100))
            write(0x6fd68610,tick_registry)
            write(tick_registry+0xc,tick_slots);write(tick_registry+0x1c,5)
            write(group+0x14,3,103);write(group+0x28,members);write(group+0x38,count)
            write(tick_slots+3*8,-2,group)
            floats(owner+0x54,0);write(owner+0x58,0)
            rows=[]
            for n in range(count):
                actor=formation_actors[n]
                machine.mem_write(actor,bytes(0x100))
                write(actor,0x6fa9129c,owner+0x200,0)
                write(actor+0x14,n,100+n);write(tick_slots+n*8,-2,actor)
                floats(actor+0x78,8,6+n);write(actor+0xd0,8,6+n)
                write(actor+0xa8,regroup_paths[0]);write(actor+0x9c,3,103)
                row=[n,100+n]+[0x24680000+16*n+k for k in range(2,11)]
                row[5]=actor;write(members+n*0x2c,*row);rows.append(row)
            pending=True;observed=[]
            def callback_boundary(uc,address,size,data):
                nonlocal pending
                actor=uc.reg_read(UC_X86_REG_ECX)
                if pending and actor==formation_actors[trigger]:
                    pending=False;uc.emu_stop()
                else:observed.append(formation_actors.index(actor))
            hook=machine.hook_add(UC_HOOK_CODE,callback_boundary,begin=0x6f16fa00,end=0x6f16fa00)
            write(stack,stop);machine.reg_write(UC_X86_REG_ESP,stack);machine.reg_write(UC_X86_REG_ECX,group)
            machine.emu_start(0x6f16bc10,stop,count=2000000)
            assert not pending and machine.reg_read(UC_X86_REG_EIP)==0x6f16fa00
            for n in range(count):
                if removed & (1<<n):
                    entry=0x6f16dd70 if action=='unbind_member' else 0x6f170fa0
                    receiver=members+n*0x2c if action=='unbind_member' else formation_actors[n]
                    call_during_callback(entry,receiver,[0])
            machine.emu_start(machine.reg_read(UC_X86_REG_EIP),stop,count=2000000)
            assert machine.reg_read(UC_X86_REG_EIP)==stop and machine.reg_read(UC_X86_REG_ESP)==stack+4
            expected_order=[n for n in reversed(range(count)) if n>=trigger or not removed & (1<<n)]
            assert observed==expected_order,(count,trigger,removed,action,observed,expected_order)
            survivors=list(rows)
            for n in reversed(range(count)):
                if removed & (1<<n):survivors[n]=survivors[-1];survivors.pop()
            assert read(group+0x38)[0]==machine.reg_read(UC_X86_REG_EAX)==len(survivors)
            actual=[read(members+n*0x2c,11) for n in range(len(survivors))]
            assert actual==survivors,(count,trigger,removed,action,actual,survivors)
            observed.clear();run(0x6f16bc10,group)
            later=[row[0] for row in reversed(survivors)]
            assert observed==later and read(group+0x38)[0]==len(survivors)
            assert [read(members+n*0x2c,11) for n in range(len(survivors))]==survivors
            callback_mutations.append(dict(count=count,trigger=trigger,removed=removed,action=action,
                callback_order=expected_order,later_callback_order=later,surviving_rows=survivors))
            machine.hook_del(hook)
    callback_fixture=json.loads((Path(__file__).parent/'fixtures/retail-callback-mutations-1.27.json').read_text())
    normalized_mutations=json.loads(json.dumps(callback_mutations))
    for case in normalized_mutations:
        for row in case['surviving_rows']:row[5]='mover'+str(row[0])
    mutation_digest=hashlib.sha256(json.dumps(normalized_mutations,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    assert callback_fixture['version']==1 and callback_fixture['binary_sha256']==digest
    assert mutation_digest==callback_fixture['cases_sha256'] and normalized_mutations==callback_fixture['cases']
    # Full completion scan through detachment, stop, notification selection and reset.
    run(0x6f004eb0,0)  # Original integer-to-soft-float initializer: -128000.
    completion_sentinel=scalar(0x6fd541dc)
    completion_threshold=float(bytes(machine.mem_read(0x6fa91e84,32)).split(bytes([0]),1)[0])
    floats(0x6fd541ac,completion_threshold)
    completion_notifications=[]
    completion_notification_counters=[]
    completion_packets=[]
    completion_events=[]
    saved_bridge=read(0x6fd3c82c)[0]
    bridge_host,bridge_payload,bridge_unit=0x10107000,0x10107100,0x10107200
    machine.mem_write(bridge_host,bytes(0x100))
    write(bridge_host+8,1)
    write(0x6fd3c82c,bridge_host)
    def observe_completion(uc,address,size,data):
        if address==0x6f071dc0:
            code,packet=read(uc.reg_read(UC_X86_REG_ESP)+4,2)
            completion_events.append((uc.reg_read(UC_X86_REG_ECX),code,read(packet+8)[0],read(packet+0x10)[0]))
            return
        if address in [0x6f170d98,0x6f170e10]:
            packet=read(uc.reg_read(UC_X86_REG_EBP)-0x30,11)
            completion_packets.append(packet)
            path_ptr=read(packet[3]+0xa8)[0]
            assert (scalar(path_ptr+0x1c),scalar(path_ptr+0x20))==(12,8)
            assert not read(path_ptr+0x88)[0]&0x100000
            return
        actor=uc.reg_read(UC_X86_REG_ECX)
        argument=read(uc.reg_read(UC_X86_REG_ESP)+4)[0] if address==0x6f170dc0 else None
        completion_notifications.append((address,actor,argument,read(actor+0x9c,2)))
        completion_notification_counters.append(read(group+0x60)[0])
    completion_hooks=[machine.hook_add(UC_HOOK_CODE,observe_completion,begin=a,end=a) for a in [0x6f170d50,0x6f170dc0,0x6f170d98,0x6f170e10,0x6f071dc0]]
    machine.ctl_flush_tb()
    completion_cases=0
    completion_rows=[]
    for count,counter,distance,member_flags,path_flags,gate,attached in itertools.product(
            [1,2],[0,18,19,20],[0,8,16,32],[0x10000,0x30000],
            [0,0x10000000,0x20000000,0x30000000],[(0,0),(1,32),(1,33)],[False,True]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,count)
        write(group+0x14,3,103)
        write(tick_slots+3*8,-2,group)
        write(group+0x60,counter)
        write(group+0x80,gate[0])
        write(group+0x6c,gate[1])
        floats(owner+0x54,0)
        write(owner+0x58,0)
        for grid,data,bitmap in zip(maps,cells,bitmaps):
            write(grid+0x88,0)
            write(grid+0xac,0xffffff,0)
            machine.mem_write(bitmap,bytes(32))
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            write(actor,0x6fa9129c,owner+0x200,0)
            write(actor+0x14,n,100+n)
            write(tick_slots+n*8,-2,actor)
            write(actor+0xd0,8,8)
            write(actor+0x9c,3,103)
            write(actor+0xa8,regroup_paths[n])
            floats(actor+0x78,8,8,0.25,0,2,0)
            floats(actor+0x90,0.25)
            write(actor+0x94,*actor_objects[n])
            write(actor+0xd8,0x10000)
            write(members+n*0x2c,n,100+n)
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x18,8+distance,8)
            write(members+n*0x2c+0x28,member_flags if n==0 else 0)
            path_ptr=regroup_paths[n]
            machine.mem_write(path_ptr,bytes(0x100))
            floats(path_ptr+0x1c,12,8,12,8,12,8)
            write(path_ptr+0x50,5)
            write(path_ptr+0x70,5,3,4)
            write(path_ptr+0x88,path_flags)
            write(path_ptr+0x94,17,29)
            for obj,grid in zip(actor_objects[n],maps):
                machine.mem_write(obj,bytes(0x80))
                write(obj+0x2c,grid)
                write(obj+0x34,0x01000001)
                write(obj+0x40,0x20000000)
        machine.mem_write(bridge_payload,bytes(0x100))
        machine.mem_write(bridge_unit,bytes(0x300))
        write(bridge_payload,0x6fa8099c)
        write(bridge_payload+0xc,0x2b61676c,0x2b616761,5,105)
        write(bridge_payload+0x54,bridge_unit)
        write(bridge_unit,0x6fb77eb0)
        write(bridge_unit+0xc,5,105)
        write(tick_registry+0x1c,6)
        write(tick_slots+5*8,-2,bridge_payload)
        write(actors[0]+0x30,bridge_payload if attached else 0)
        active=not gate[0] or gate[1]>32
        retry=active and count>1 and bool(member_flags&0x20000) and counter+1<20 and distance*distance>completion_threshold
        complete=active and not retry
        completion_notifications.clear()
        completion_notification_counters.clear()
        completion_packets.clear()
        completion_events.clear()
        run(0x6f16c390,group)
        actor,path_ptr=actors[0],regroup_paths[0]
        assert read(group+0x38)[0]==count
        assert read(group+0x60)[0]==(0 if complete else counter+int(active))
        assert read(members,2)==([0xffffffff]*2 if complete else [0,100])
        assert read(members+0x14)[0]==(0 if complete else actor)
        assert read(actor+0x9c,2)==([0xffffffff]*2 if complete else [3,103])
        assert scalar(actor+0x80)==(0 if complete else 0.25)
        assert bool(read(actor+0xd8)[0]&0x10000)==(not complete)
        assert read(path_ptr+0x50)[0]==(0 if active else 5)
        assert read(path_ptr+0x70,3)==([0,0xffffffff,0xffffffff] if active else [5,3,4])
        assert read(path_ptr+0x94,2)==([0,0] if active else [17,29])
        assert bool(read(path_ptr+0x88)[0]&0x100000)==complete
        assert (scalar(path_ptr+0x1c),scalar(path_ptr+0x20))==((completion_sentinel,completion_sentinel) if complete else (12,8))
        assert (scalar(path_ptr+0x24),scalar(path_ptr+0x28))==((completion_sentinel,completion_sentinel) if complete else (12,8))
        assert (scalar(path_ptr+0x2c),scalar(path_ptr+0x30))==(12,8)
        expected=[]
        if complete:
            blocked=bool(member_flags&0x20000)
            partial=int(bool(path_flags&0x10000000) and not path_flags&0x20000000)
            expected=[(0x6f170dc0 if blocked else 0x6f170d50,actor,partial if blocked else None,[0xffffffff]*2)]
        assert completion_notifications==expected
        assert completion_notification_counters==([counter+1] if complete else [])
        expected_packets=[]
        if complete:
            tag=0x63702670-partial if blocked else 0x63702661
            expected_packets=[[0x5e70726f,0x60706375,tag,actor,0,0,0,0,0,0xffffffff,0xffffffff]]
        assert completion_packets==expected_packets
        event_code=0x40190066 if member_flags&0x20000 else 0x40190065
        assert completion_events==([(bridge_unit,event_code,event_code,bridge_unit)] if complete and attached else [])
        assert read(members+0x2c,2)==[1,101]
        assert scalar(actors[1]+0x80)==0.25
        completion_rows.append(dict(count=count,counter=counter,distance=distance,member_flags=member_flags,
            path_flags=path_flags,gate=list(gate),attached=attached,retry=retry,complete=complete,
            next_counter=read(group+0x60)[0],member_identity=read(members,2),
            fine_count=read(path_ptr+0x50)[0],adaptive_count=read(path_ptr+0x70)[0],
            indices=read(path_ptr+0x74,2),retry_delay=read(path_ptr+0x94,2),
            next_path_flags=read(path_ptr+0x88)[0],destination=read(path_ptr+0x1c,2),
            notifications=list(completion_notifications),notification_counters=list(completion_notification_counters),events=list(completion_events)))
        run(0x6f16bc10,group)
        assert read(group+0x38)[0]==count-int(complete)
        assert machine.reg_read(UC_X86_REG_EAX)==count-int(complete)
        if count-int(complete):
            assert read(members,2)==([1,101] if complete else [0,100])
            assert read(members+0x14)[0]==actors[1 if complete else 0]
        completion_cases+=1
    if args.completion_fixture:
        result=dict(version=1,binary_sha256=digest,function='6f16c390',reset_function='6f168740',
            threshold=completion_threshold,cases=completion_rows,
            scope='Complete unmodified original completion scans plus following preparation; controlled actors, half include real CUnit event bridge, no gameplay subscribers')
        if args.completion_reference:
            frozen=args.completion_reference.read_bytes()
            if args.completion_reference.suffix=='.gz':frozen=gzip.decompress(frozen)
            assert json.loads(json.dumps(result))==json.loads(frozen),'completion fixture differs from original'
        args.completion_fixture.write_text(json.dumps(result,indent=1)+'\n')
        args.report.write_text(json.dumps(dict(binary_sha256=digest,passed=True,cases=completion_cases,threshold=completion_threshold,
            fixture_sha256=hashlib.sha256(args.completion_fixture.read_bytes()).hexdigest()),indent=1)+'\n')
        return
    for hook in completion_hooks: machine.hook_del(hook)
    write(0x6fd3c82c,saved_bridge)
    # Bounded original dispatch prefixes: stop at the real movement handler,
    # before order-side effects. Each case rebuilds the subscriber fixture.
    subscriber_table,subscriber_buckets,subscriber_node,move_ability,event_packet=[0x10108000+n for n in [0,0x100,0x200,0x300,0x600]]
    subscriber_prefix_cases=0
    for event_code,internal_code,handler in [(0x40190065,0xd0196,0x6f5fa7a0),(0x40190066,0xd0198,0x6f603110)]:
        for bucket_count,prior_refs in itertools.product([1,4,16],[1,3]):
            machine.mem_write(bridge_unit,bytes(0x300))
            machine.mem_write(subscriber_table,bytes(0x700))
            write(bridge_unit,0x6fb77eb0,prior_refs,subscriber_table)
            write(subscriber_table,(1<<16)|(bucket_count<<8),subscriber_buckets)
            write(subscriber_buckets+(event_code&(bucket_count-1))*4,subscriber_node)
            write(subscriber_node,subscriber_node,event_code,move_ability,internal_code)
            write(move_ability,0x6fb62794,1)
            write(move_ability+0x30,bridge_unit)
            write(event_packet,0,0,event_code,0,bridge_unit)
            write(0,0)
            write(stack,stop,event_code,event_packet)
            machine.reg_write(UC_X86_REG_ESP,stack)
            machine.reg_write(UC_X86_REG_ECX,bridge_unit)
            machine.emu_start(0x6f071dc0,handler,count=100000)
            assert machine.reg_read(UC_X86_REG_EIP)==handler
            assert machine.reg_read(UC_X86_REG_ECX)==move_ability
            assert read(event_packet+8)[0]==internal_code
            assert read(event_packet+0x10)[0]==bridge_unit
            assert read(bridge_unit+4)[0]==prior_refs+1
            assert bytes(machine.mem_read(subscriber_table,1))==b'\x01'
            subscriber_prefix_cases+=1
    write(0,0)  # Discard the deliberately suspended prefix's SEH frame.
    # Observe queue-head mutation, then resume through original deferred release.
    queue_prefix_cases=0
    release_callback_times=[]
    def observe_release_callback(uc,address,size,data):
        release_callback_times.append(scalar(owner+0x14+0x40))
    release_hook=machine.hook_add(UC_HOOK_CODE,observe_release_callback,begin=0x6f15e500,end=0x6f15e500)
    machine.ctl_flush_tb()
    queue_wrappers=[0x10109000+n*0x100 for n in range(3)]
    queue_orders=[0x10109300+n*0x100 for n in range(3)]
    movement_codes=list(range(0xd016b,0xd0175))
    for next_state,code,order_flags,current_valid in itertools.product(
            ['none','valid','stale','deleted','wrong_type','inactive'],movement_codes+[0xd0144,0xd0196],[0,4,0x84],[False,True]):
        machine.mem_write(bridge_unit,bytes(0x300))
        machine.mem_write(tick_registry,bytes(0x100))
        write(0x6fd68610,tick_registry)
        write(tick_registry+0xc,tick_slots)
        write(tick_registry+0x1c,3)
        for n,(wrapper_ptr,order_ptr) in enumerate(zip(queue_wrappers,queue_orders)):
            machine.mem_write(wrapper_ptr,bytes(0x100))
            machine.mem_write(order_ptr,bytes(0x100))
            write(tick_slots+n*8,-2,wrapper_ptr)
            write(wrapper_ptr+0xc,0x2b61676c,0x2b616761,n,100+n)
            write(wrapper_ptr+0x54,order_ptr)
            write(order_ptr+0xc,n,100+n)
        write(bridge_unit+0x174,0,100)
        write(bridge_unit+0x19c,2,102 if current_valid else 999)
        write(queue_orders[0]+0x24,*([0xffffffff,0xffffffff] if next_state=='none' else [1,999 if next_state=='stale' else 101]))
        write(queue_orders[0]+0x30,code)
        write(queue_orders[2]+0x20,order_flags)
        if next_state=='deleted': write(tick_slots+8+4,0)
        if next_state=='wrong_type': write(queue_wrappers[1]+0xc,0)
        if next_state=='inactive': write(queue_wrappers[1]+0x20,1)
        write(queue_orders[0],0x6fb7886c)  # Original COrderPoint vtable.
        release_clock=owner+0x14
        release_header,release_heap=0x1010a000,0x1010a100
        machine.mem_write(release_clock,bytes(0x54))
        machine.mem_write(release_header,bytes(0x100))
        machine.mem_write(release_heap,bytes(0x100))
        write(release_clock+0x10,release_heap)
        write(release_clock+0x1c,16,1)
        write(release_clock+0x38,release_header)
        floats(release_clock+0x40,0.5)
        write(release_clock+0x50,17)
        cleanup_request=release_header+0x80
        write(bridge_unit+0x18c,cleanup_request,0x86)
        write(queue_orders[0]+4,2)  # Retain one external payload reference.
        before=bytes(machine.mem_read(queue_orders[0],0x100))
        write(stack,stop)
        machine.reg_write(UC_X86_REG_ESP,stack)
        machine.reg_write(UC_X86_REG_ECX,bridge_unit)
        machine.ctl_flush_tb()  # This boundary was crossed by the preceding resumed case.
        machine.emu_start(0x6f691260,0x6f691331,count=100000)
        assert machine.reg_read(UC_X86_REG_EIP)==0x6f691331,(next_state,code,order_flags,current_valid,hex(machine.reg_read(UC_X86_REG_EIP)))
        assert read(bridge_unit+0x174,2)==([1,101] if next_state=='valid' else [0xffffffff]*2)
        assert read(bridge_unit+0x19c,2)==[2,102 if current_valid else 999]
        assert read(queue_orders[2]+0x20)[0]==(order_flags&~4 if current_valid and code in movement_codes else order_flags)
        assert bytes(machine.mem_read(queue_orders[0],0x100))==before
        machine.emu_start(0x6f691331,stop,count=100000)
        assert machine.reg_read(UC_X86_REG_EIP)==stop
        request=release_header+4
        assert read(queue_wrappers[0]+0x20)[0]==request
        assert read(queue_wrappers[0]+0x54)[0]==queue_orders[0]
        assert read(tick_slots+4)[0]==queue_wrappers[0]
        assert read(release_clock+0x38,2)==[0,1]
        assert read(release_clock+0x50)[0]==18
        assert read(release_clock+0x20)[0]==2
        assert read(release_heap+4)[0]==request
        assert read(request+0xc,5)==[release_clock,0x20000,18,queue_wrappers[0],0]
        assert abs(scalar(request+4)-(0.5+time_deadzone))<0.0000001
        assert scalar(request+8)==time_deadzone
        assert read(bridge_unit+0x18c,2)==[0,0x80]
        assert read(cleanup_request+0x10)[0]==0x10000
        # Scheduled wrappers remain registered but are no longer active agents.
        machine.reg_write(UC_X86_REG_EDX,100)
        run(0x6f061320,0)
        assert machine.reg_read(UC_X86_REG_EAX)==0
        run(0x6f0557b0,queue_orders[0])
        assert read(queue_wrappers[0]+0x20)[0]==request
        assert read(release_clock+0x3c)[0]==1
        assert read(release_clock+0x20)[0]==2
        assert read(release_clock+0x50)[0]==18
        # Empty relationships isolate wrapper release. Half use the original
        # owner notification callback with one externally retained payload ref.
        wrapper_pool=0x1010a200
        machine.mem_write(wrapper_pool,bytes(0x20))
        write(wrapper_pool+0x18,1)
        write(bridge_host+0x34,wrapper_pool)
        write(0x6fd3c82c,bridge_host)
        notify_owner=bool(queue_prefix_cases%2)
        write(owner+0x254,0x6f04d9c0 if notify_owner else 0)
        write(queue_wrappers[0],0x6fa8099c)
        write(tick_registry+0x40,0xffffffff)
        write(tick_registry+0x48,3)
        deadline=scalar(request+4)
        prior_callbacks=len(release_callback_times)
        run(0x6f052380,release_clock)  # Before the deadline: nothing released.
        assert len(release_callback_times)==prior_callbacks
        assert read(release_clock+0x20)[0]==2
        assert read(tick_slots+4)[0]==queue_wrappers[0]
        now=deadline if queue_prefix_cases%2 else 0.75
        floats(release_clock+0x40,now)
        run(0x6f052380,release_clock)
        assert release_callback_times[prior_callbacks:]==[deadline]
        assert scalar(release_clock+0x40)==now
        assert read(release_clock+0x20)[0]==1
        assert read(release_clock+0x38,2)==[release_header,0]
        assert read(request+0x10)[0]==0x10000
        assert read(queue_wrappers[0]+0x14,4)==[0xffffffff,0xffffffff,0,0]
        assert read(queue_wrappers[0]+0x50,2)==[0,0]
        assert read(tick_slots,2)==[0xffffffff,0]
        assert read(tick_registry+0x40)[0]==0
        assert read(tick_registry+0x48)[0]==2
        assert read(wrapper_pool+0x14,2)==[queue_wrappers[0]-4,0]
        assert read(queue_wrappers[0]-4)[0]==0
        expected_payload=bytearray(before)
        if notify_owner:
            struct.pack_into('<I',expected_payload,4,1)
            struct.pack_into('<II',expected_payload,0xc,0xffffffff,0xffffffff)
            assert read(0x6fcd574c)[0]==0x40190064
            assert read(0x6fcd5754)[0]==queue_orders[0]
        assert bytes(machine.mem_read(queue_orders[0],0x100))==expected_payload
        run(0x6f052380,release_clock)  # Empty drain is idempotent.
        assert len(release_callback_times)==prior_callbacks+1
        assert read(release_clock+0x38,2)==[release_header,0]
        queue_prefix_cases+=1
    machine.hook_del(release_hook)
    write(0x6fd3c82c,saved_bridge)
    group_cases=0
    for desired,caps,group_flags,member_flags,mover_flags,published in itertools.product(
            [0,0.25,1,4],[(0.5,2),(2,0.5)],[0,8],[0,0x10000,0x200000],[0,0x1000000],[None,0.25,1,4,sentinel]):
        machine.mem_write(group,bytes(0x100))
        machine.mem_write(members,bytes(0x100))
        write(group+0x28,members)
        write(group+0x38,2)
        write(group+0x80,group_flags)
        write(group+0x7c,auxiliary if published is not None else 0)
        floats(auxiliary+0x20,0 if published is None else published,sentinel)
        clock=owner+0x14
        floats(clock+0x40,0)
        write(clock+0x44,0)
        for grid,data,bitmap in zip(maps,cells,bitmaps):
            write(grid+0x88,0)
            write(grid+0xac,0xffffff,0)
            machine.mem_write(bitmap,bytes(32))
            machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
        for n,actor in enumerate(actors):
            machine.mem_write(actor,bytes(0x100))
            write(actor,0x6fa9129c,owner+0x200,0)
            floats(actor+0x78,6+4*n,8,0,0,caps[n],0)
            floats(actor+0x90,0.25)
            write(actor+0x94,*actor_objects[n])
            write(actor+0xd8,mover_flags if n else 0)
            write(members+n*0x2c+0x14,actor)
            floats(members+n*0x2c+0x20,desired,0)
            write(members+n*0x2c+0x28,member_flags if n else 0)
            for obj,grid in zip(actor_objects[n],maps):
                machine.mem_write(obj,bytes(0x80))
                write(obj+0x2c,grid)
                write(obj+0x34,0x01000001)
        run(0x6f16c570,group,vector)
        shared=not(group_flags&8 or member_flags&0x210000 or mover_flags&0x1000000)
        cap=(min(caps) if published is None or published==sentinel else published) if shared else sentinel
        assert scalar(auxiliary+0x24)==(min(caps) if shared and published is not None else sentinel)
        for n,actor in enumerate(actors):
            submitted=min(desired,cap)
            assert scalar(actor+0xc0)==submitted
            assert abs(scalar(actor+0x80)-min(submitted,caps[n]))<0.00001
            assert abs(scalar(actor+0x84))<0.00001
            assert (scalar(actor+0x78),scalar(actor+0x7c))==(6+4*n,8)
            assert scalar(members+n*0x2c+0x20)==desired
        # Failed-route stop: integrate old velocity, stop every mover, unlink
        # both pending paths from each of the four scheduler bucket policies.
        paths=[system+0x17000,system+0x17100]
        bucket_offset,policy,budget=[(0,0,500),(0x1c,0x4000000,500),(0x38,0,400),(0x54,0x2000000,500)][group_cases%4]
        table=0x6fd53a90
        machine.mem_write(table,bytes(0x70))
        write(table+0x38,400)
        bucket=table+bucket_offset
        write(bucket+0x10,2,*paths)
        old_positions=[]
        old_headings=[]
        for n,actor in enumerate(actors):
            path=paths[n]
            machine.mem_write(path,bytes(0x100))
            write(path+0x84,(budget<<16)|700)
            write(path+0x88,policy|0x1000)
            write(path+0x8c,paths[0] if n else -1,paths[1] if not n else -1)
            write(actor+0xa8,path)
            old_positions.append((scalar(actor+0x78)+scalar(actor+0x80)*0.25,scalar(actor+0x7c)+scalar(actor+0x84)*0.25))
            old_headings.append(scalar(actor+0x8c))
        floats(clock+0x40,0.25)
        run(0x6f16c5d0,group)
        assert read(bucket+0x10,3)==[0,0,0]
        for n,actor in enumerate(actors):
            assert scalar(members+n*0x2c+0x20)==0
            assert scalar(members+n*0x2c+0x24)==old_headings[n]
            assert scalar(actor+0xc0)==0
            assert (scalar(actor+0x80),scalar(actor+0x84))==(0,0)
            assert max(abs(scalar(actor+0x78+k*4)-old_positions[n][k]) for k in [0,1])<0.000002
            assert scalar(actor+0x70)==0.25
            assert not(read(actor_objects[n][1]+0x40)[0]&0x20000000)
            assert read(paths[n]+0x88,3)==[(policy|0x1000)&~0x2000000,0,0]
        group_cases+=1
    # Complete arrival dispatch: real support-height query, callbacks and unwind.
    machine.mem_map(0x10300000,0x20000)
    machine.mem_map(0x10400000,0x200000)
    terrain,terrain_vertices,support_unit,support_mover,support_bridges,pending_timer=[0x10300000+n for n in [0,0x3000,0x4000,0x5000,0x6000,0x6200]]
    write(0x6fd726c0,terrain)
    write(0x6fd726cc,support_bridges)
    write(terrain,1)
    write(terrain+0xb4,4,4,16,16)
    write(terrain+0xe0,25,terrain_vertices)
    support_trace=[]
    presentation_trace=[]
    presentation_samples=[]
    presentation_angles=[]
    adaptive_requests=[]
    owner_events=[]
    owner_frames=[]
    def observe_support(uc,address,size,data):
        if whole_owner and address in owner_addresses+[0x6f16c150]:owner_events.append(address)
        if address in owner_addresses:return
        if address in adaptive_addresses:
            if not adaptive:return
            if address==0x6f162cb0:
                lane,route,source,goal,budget,size_class,allow_partial=read(uc.reg_read(UC_X86_REG_ESP)+4,7)
                assert uc.reg_read(UC_X86_REG_ECX)==acc_system
                adaptive_requests.append(dict(lane=lane,path='group' if route==accepted_path+0x54 else 'member',
                    route_struct=route,source=[scalar(source+k*4) for k in range(2)],goal=[scalar(goal+k*4) for k in range(2)],
                    budget=budget,size_input=size_class,allow_partial=allow_partial))
            elif address==0x6f166d80:
                request=adaptive_requests[-1]
                route=request['route_struct']
                pointer=read(route+0xc)[0]
                request.update(result=uc.reg_read(UC_X86_REG_EAX),popped=read(acc_system+0x9c)[0],
                    route=[scalar(pointer+k*4) for k in range(read(route+0x1c)[0]*2)])
            return
        if address in presentation_addresses:
            presentation_trace.append(address)
            if address==0x6f743810:
                descriptor,point=read(uc.reg_read(UC_X86_REG_ESP)+4,2)
                assert descriptor==height_layer
                presentation_samples.append([scalar(point),scalar(point+4)])
            if address==0x6f1c6c40:
                presentation_angles.append([scalar(uc.reg_read(UC_X86_REG_ESP)+4+4*k) for k in range(3)])
            return
        support_trace.append(address)
        if address==0x6f73c800:
            assert read(uc.reg_read(UC_X86_REG_ESP)+8)[0]==7
        if address==0x6f148100:
            arguments=read(uc.reg_read(UC_X86_REG_ESP)+4,8)
            assert uc.reg_read(UC_X86_REG_ECX)==fine_system
            assert arguments[0]==tick_path+0x34
            if not adaptive:assert [scalar(arguments[1]+k*4) for k in range(2)]==list(position)
            if not adaptive:assert [scalar(arguments[2]+k*4) for k in range(2)]==target
            assert read(arguments[3])[0]==0x02000000
            assert arguments[4]==700 and scalar(arguments[5])==0.25
        if address==0x6f5ffb60:
            packet=read(uc.reg_read(UC_X86_REG_ESP)+4)[0]
            assert uc.reg_read(UC_X86_REG_ECX)==move_ability
            assert read(packet+8,2)==[0xd016c,next_task]
            assert read(move_ability+0x20)[0]==0  # No paused/suppressed mode bypass.
    observed_addresses=[0x6f16c150,0x6f16ce10,0x6f165ae0,0x6f167ce0,0x6f148100,0x6f14a4c0,0x6f5fc900,0x6f169910,0x6f169840,0x6f16bcf0,0x6f169c50,0x6f169d60,0x6f16b7b0,0x6f16de50,0x6f169a30,0x6f691260,0x6f67df00,0x6f67de20,0x6f694a10,0x6f5ffb60,0x6f05b440,0x6f05b970,0x6f5fa7a0,0x6f69a840,0x6f171340,0x6f684480,0x6f73c800,0x6f745110,0x6f170d50,0x6f170dc0]
    presentation_addresses=[0x6f66d990,0x6f743810,0x6f782a80,0x6f1c6c40,0x6f1c7ba0,int(resolved_crt_exports['_libm_sse2_sqrt_precise']['entry'],16)]
    adaptive_addresses=[0x6f162cb0,0x6f166d80]
    owner_addresses=[0x6f15aa80,0x6f167310,0x6f1705c0,0x6f170cf0,0x6f170800,0x6f1702f0]
    observed_addresses+=presentation_addresses+adaptive_addresses+owner_addresses
    support_hooks=[machine.hook_add(UC_HOOK_CODE,observe_support,begin=a,end=a) for a in observed_addresses]
    machine.ctl_flush_tb()
    arrival_dispatch_cases=0
    active_arrival_dispatch_cases=0
    arrival_next_task_rejection_cases=0
    arrival_next_task_acceptance_cases=0
    arrival_fresh_tick_cases=0
    elapsed_arrival_cases=[]
    stock_ui_arrival_cases=[]
    slope_arrival_cases=[]
    obstacle_arrival_cases=[]
    adaptive_arrival_cases=[]
    owner_arrival_cases=[]
    primary_arrival_cases=[]
    owner_separation_cases=[]
    active_pair_cases=[]
    arrival_task_wrapper,arrival_task,arrival_release_header,arrival_release_heap=[0x10300000+n for n in [0x8200,0x8400,0x8600,0x8700]]
    next_task_wrapper,next_task=[0x10300000+n for n in [0x8800,0x8a00]]
    accepted_generator,accepted_group,accepted_path,accepted_members,accepted_profile,accepted_profile_buckets,accepted_nodes=[0x10300000+n for n in [0xa004,0xb004,0xc004,0xd000,0xe000,0xe300,0xf000]]
    for entry in [0x6f016290,0x6f0162b0,0x6f0162c0,0x6f0162d0,0x6f016210,0x6f016220,0x6f016230,0x6f016240]:
        run(entry,0)  # Authentic speed-bound startup: min1/max522 and defaults.
    def setup_separation_pair(second_position):
        write(accepted_profile+0x224,0,0,0,0)
        run(0x6f66fc50,support_unit)
        assert machine.reg_read(UC_X86_REG_EAX)==0
        run(0x6f693d50,support_unit)
        assert read(support_mover+0xac)[0]==0
        run(0x6f004790,0)
        sep_nodes=[0x10510004,0x10510104]
        for n,sep in enumerate(sep_nodes):
            machine.mem_write(sep-4,bytes(0x40))
            write(sep-4,sep_nodes[n+1]-4 if n==0 else 0)
            write(sep,0x6fa917f0)
        write(owner+0x7f8+0x14,sep_nodes[0]-4,0,0)
        write(accepted_profile+0x224,1)
        run(0x6f66fc50,support_unit)
        assert machine.reg_read(UC_X86_REG_EAX)==1
        run(0x6f693d50,support_unit)
        assert read(support_mover+0xac)[0]==sep_nodes[0]
        second_mover,second_path=0x10510200,0x10510400
        machine.mem_write(second_mover,bytes(0x100))
        write(second_mover,0x6fa9129c)
        write(second_mover+0xc,0x5e70726f,0x60706375,8,105)
        write(second_mover+0x9c,-1,-1)
        write(second_mover+0xa8,second_path)
        run(0x6f1657c0,second_path)
        write(second_path+0x9c,0x02000000)
        floats(second_mover+0x70,scalar(arrival_release_clock+0x40))
        floats(second_mover+0x78,*second_position,0,0)
        floats(second_mover+0x90,.25)
        write(second_mover+0x94,*actor_objects[1])
        write(second_mover+0xd0,*(math.floor(v) for v in second_position))
        write(support_mover+0xc,0x5e70726f,0x60706375)
        for actor,objs in [(support_mover,actor_objects[0]),(second_mover,actor_objects[1])]:
            for obj,grid in zip(objs,maps):
                if actor==second_mover:
                    machine.mem_write(obj,bytes(0x80))
                    write(obj+0x2c,grid)
                    write(obj+0x34,0x01000001)
                if grid==maps[0]:write(obj+0x34,1)
                write(obj+0x30,actor)
        floats(displacement,0,0)
        run(0x6f1603d0,second_mover,displacement)
        run(0x6f1710e0,second_mover,1,0,0,0)
        assert read(second_mover+0xac)[0]==sep_nodes[1]
        assert read(owner+0x51c)[0]==sep_nodes[1]
        write(owner+0x540+0xc,0x10511000)
        write(owner+0x540+0x14,0,128,0)
        return sep_nodes,second_mover,second_path
    for position,height,subscriptions,unit_status,task_mode in itertools.product([(4,4),(4.25,5.5),(10,11)],[-32,0,64],[1,2],[0,0x10000],[0,1,2,3,4,5,6,7,8,9,10,11,12]):
        if task_mode>=11 and not (args.primary_route_fixture or args.primary_route_reference):continue
        if task_mode>=5 and (position,height,subscriptions,unit_status)!=((4,4),0,1,0):continue
        active_task=task_mode!=0
        reject_next=task_mode==2
        accept_next=task_mode>=3
        stock_ui=task_mode>=4
        slope=0.125 if task_mode==5 else 0
        obstacle=task_mode>=6
        adaptive=task_mode>=8
        whole_owner=task_mode>=9
        primary_route=task_mode>=11
        active_pair=task_mode==10
        pair_ready=False
        owner_events.clear()
        owner_frames.clear()
        blocked={(6,y) for y in range(2,7)} if obstacle else set()
        goal_offset=4 if obstacle else 2
        has_next=reject_next or accept_next
        for y in range(5):
            for x in range(5):
                v=terrain_vertices+(y*5+x)*0x1c
                write(v,0x80402000+int((height+x*128*slope)*4)+x*0x800000+y*0x4000,y*5+x)
        machine.mem_write(bridge_host,bytes(0x100))
        machine.mem_write(support_unit,bytes(0x400))
        machine.mem_write(support_mover,bytes(0x100))
        write(bridge_host+8,1)
        write(0x6fd3c82c,bridge_host)
        write(0x6fd68610,tick_registry)
        write(tick_registry+0xc,tick_slots)
        write(tick_registry+0x1c,2)
        write(tick_slots,-2,support_mover,-2,0x10308000)
        machine.mem_write(0x10308000,bytes(0x100))
        write(0x10308000,0x6fa8099c)
        write(0x1030800c,0x2b61676c,0x2b616761,1,101)
        write(0x10308054,support_unit)
        write(support_mover,0x6fa9129c,0 if whole_owner else owner+0x200,0)
        if whole_owner:
            write(owner+0x38c,0)  # No shared group record in this singleton.
            write(owner+0x43c,0,0)
            write(owner+0x51c,0)  # No separation/repulsor object registered.
            write(owner+0x53c,0)
            run(0x6f004200,0)
            run(0x6f004210,0)
        write(support_mover+0x14,0,100)
        floats(support_mover+0x70,0)
        floats(support_mover+0x78,*position,0.25,0)
        floats(support_mover+0xc8,0.25)
        floats(owner+0x54,0)
        write(owner+0x58,0)
        write(support_unit,0x6fb77eb0,1)
        write(support_unit+0xc,1,101)
        write(support_unit+0x174,-1,-1)
        write(support_unit+0x19c,-1,-1)  # Separate user-order queue is empty.
        if active_task:
            # CTaskPoint is the internal movement task, distinct from COrderPoint.
            # Its authentic factory initializes FloatMini X/Y/range at 38/40/48.
            machine.mem_write(arrival_task,bytes(0x100))
            write(0x6fd70f1c,0x6fb78eb0)
            run(0x6f06a270,0x6fd70f20,0x50,1)
            write(0x6fd70f30,arrival_task)
            run(0x6f680db0,0x6fd70f1c)
            assert machine.reg_read(UC_X86_REG_EAX)==arrival_task
            assert read(arrival_task)[0]==0x6fb78ec0
            assert read(arrival_task+0x24,2)==[0xffffffff]*2
            write(arrival_task+4,1)
            write(arrival_task+0xc,2,102)
            write(arrival_task+0x30,0xd016b+active_arrival_dispatch_cases%2)
            machine.mem_write(arrival_task_wrapper,bytes(0x100))
            write(arrival_task_wrapper,0x6fa8099c)
            write(arrival_task_wrapper+0xc,0x2b61676c,0x2b616761,2,102)
            write(arrival_task_wrapper+0x54,arrival_task)
            write(tick_registry+0x1c,3)
            write(tick_slots+16,-2,arrival_task_wrapper)
            write(support_unit+0x174,2,102)
            arrival_release_clock=owner+0x14
            machine.mem_write(arrival_release_clock,bytes(0x54))
            machine.mem_write(arrival_release_header,bytes(0x100))
            machine.mem_write(arrival_release_heap,bytes(0x100))
            write(arrival_release_clock+0x10,arrival_release_heap)
            write(arrival_release_clock+0x1c,16,1)
            write(arrival_release_clock+0x38,arrival_release_header)
            if has_next:
                machine.mem_write(next_task,bytes(0x100))
                write(0x6fd70f30,next_task)
                run(0x6f680db0,0x6fd70f1c)
                assert machine.reg_read(UC_X86_REG_EAX)==next_task
                assert read(next_task)[0]==0x6fb78ec0
                write(next_task+4,1)
                write(next_task+0xc,3,103)
                write(next_task+0x30,0xd016c)
                floats(next_task+0x38,(position[0]+(goal_offset if accept_next else 0))*32)
                floats(next_task+0x40,position[1]*32)
                machine.mem_write(next_task_wrapper,bytes(0x100))
                write(next_task_wrapper,0x6fa8099c)
                write(next_task_wrapper+0xc,0x2b61676c,0x2b616761,3,103)
                write(next_task_wrapper+0x54,next_task)
                write(tick_registry+0x1c,4)
                write(tick_slots+24,-2,next_task_wrapper)
                write(arrival_task+0x24,3,103)
                if reject_next: write(arrival_release_header,arrival_release_header+0x40)
            arrival_task_before=bytes(machine.mem_read(arrival_task,0x50))
        write(support_unit+0x164,0x6fac2f10,0,0,100)
        floats(support_unit+0x284,position[0]*32,position[1]*32,123,0.25)
        write(owner+0x24c,fine_system)
        write(fine_system+0x1c,maps[1])
        machine.mem_write(subscriber_table,bytes(0x700))
        write(support_unit+8,subscriber_table)
        write(support_unit+0x5c,unit_status)
        write(subscriber_table,((subscriptions+int(has_next))<<16)|(4<<8),subscriber_buckets)
        write(subscriber_buckets+4,subscriber_node)
        write(subscriber_node,subscriber_node,0x40190065,move_ability,0xd0196)
        if subscriptions==2:
            write(subscriber_buckets+8,subscriber_node+0x20)
            write(subscriber_node+0x20,subscriber_node+0x20,0x40190066,move_ability,0xd0198)
        if has_next:
            write(subscriber_buckets,subscriber_node+0x40)
            write(subscriber_node+0x40,subscriber_node+0x40,0xd016c,move_ability,0xd016c)
        write(move_ability,0x6fb62794,subscriptions+1+int(has_next))
        write(move_ability+0x20,4 if active_task else 0)
        write(move_ability+0x30,support_unit)
        write(move_ability+0xcc,-1,-1)
        write(move_ability+0xd8,-1,-1)
        write(move_ability+0x9c,pending_timer,0x86)
        write(pending_timer+0x10,0)
        write(event_packet,0,0,0x40190065,0,support_unit)
        write(support_mover+0xa8,tick_path)
        write(support_mover+0x9c,-1,-1)
        floats(support_mover+0x90,0.25)
        write(support_mover+0x94,*actor_objects[0])
        write(support_mover+0xd0,int(position[0]),int(position[1]))
        machine.mem_write(tick_path,bytes(0x100))
        if adaptive:
            # Genuine CLrPath activation establishes accelerator flag200000.
            run(0x6f1657c0,tick_path)
            write(tick_registry+0x1c,8)
            write(tick_registry+0x40,7)
            write(tick_registry+0x48,4,0,104)
            write(tick_slots+7*8,-1,0)
            run(0x6f166060,tick_path,0)
            assert read(tick_path+0x14,2)==[7,104]
            assert read(tick_path+0x88)[0]==0x200000
        floats(tick_path+0x1c,8,8,8,8,8,8)
        for obj,grid in zip(actor_objects[0],maps):
            machine.mem_write(obj,bytes(0x80))
            write(obj+0x2c,grid)
            write(obj+0x34,0x01000001)
        if accept_next:
            # Maps and objects begin empty together; arrival's real integration
            # inserts initial occupancy, retained throughout the later trajectory.
            for grid,data,bitmap in zip(maps,cells,bitmaps):
                write(grid+0x88,0)
                write(grid+0xac,0xffffff,0)
                machine.mem_write(bitmap,bytes(32))
                machine.mem_write(data,struct.pack('<256I',*([0xffffff]*256)))
            for x,y in blocked:write(cells[1]+(y*16+x)*4,0x02ffffff)
            write(support_mover+0x30,0x10308000)
            # Authentic positive speed producer, no attached buff abilities.
            write(support_unit+0x1dc,-1,-1)
            write(support_unit+0x30,0x68666f6f)
            floats(move_ability+0x70,256)
            floats(move_ability+0x78,1)
            write(0x6fd687a8,0x10310400)
            write(0x103107e0,1)
            write(support_unit+0x240,-1,-1)  # No target association for this point task.
            write(accepted_profile_buckets,0,0,accepted_profile)
            write(accepted_profile+0x14,0x68666f6f)
            run(0x6f198420,accepted_profile+0x14)
            write(accepted_profile,machine.reg_read(UC_X86_REG_EAX))
            floats(accepted_profile+0x1d8,1,522)
            write(0x6fd709f4,accepted_profile_buckets)
            write(0x6fd709fc,0)
            floats(bridge_host+0x6c,0,0,512,512,0,522)
            # Recycled objects use original activation; group member storage is retained.
            for ptr,ctor,pool_offset in [(accepted_generator,0x6f169220,0x638),(accepted_group,0x6f14fc40,0x678),(accepted_path,0x6f1657c0,0x958)]:
                machine.mem_write(ptr-4,bytes(0x200))
                if ptr==accepted_group:
                    # Recycled CPrCluster with its retained twelve-row allocation.
                    # First construction14fc40 allocates through external Storm.
                    write(ptr,0x6fa90d64)
                    write(ptr+0x14,-1,-1)
                    write(ptr+0x1c,0x6fa90d5c,accepted_members,12*0x2c,accepted_members,12*0x2c,0,12,0)
                    write(ptr+0x40,-1,-1)
                else:
                    run(ctor,ptr)
                machine.mem_write(owner+pool_offset,bytes(0x20))
                write(owner+pool_offset+0x14,ptr-4,0,0)
            machine.mem_write(accepted_members,bytes(12*0x2c))
            write(accepted_group+0x28,accepted_members)
            write(accepted_group+0x34,12)
            write(owner+0x3b4,0,0)
            write(tick_registry+0x1c,8 if adaptive else 7)
            write(tick_registry+0x40,4)
            write(tick_registry+0x48,4+int(adaptive),0,200)
            for idx in [4,5,6]: write(tick_slots+idx*8,idx+1 if idx<6 else -1,0)
            run(0x6f06a270,0x6fd3cce4,0x10,2)
            write(0x6fd3ccf4,accepted_nodes)
            write(accepted_nodes,accepted_nodes+0x10)
            write(accepted_nodes+0x10,0)
            write(owner+0x250,acc_system)
        support_trace.clear()
        run(0x6f071dc0,support_unit,0x40190065,event_packet)
        assert read(0)[0]==0
        assert read(support_unit+4)[0]==1
        assert read(move_ability+4)[0]==1+int(has_next)+2*int(accept_next)
        assert read(subscriber_table)[0]==0x400+((int(has_next)+2*int(accept_next))<<16)
        assert read(subscriber_node+8)[0]==0
        if subscriptions==2: assert read(subscriber_node+0x28)[0]==0
        assert read(move_ability+0x9c,2)==[0,0x80]
        assert read(pending_timer+0x10)[0]==0x10000
        assert read(move_ability+0xcc,2)==[0xffffffff]*2
        assert read(move_ability+0xd8,2)==[0xffffffff]*2
        assert read(event_packet+8)[0]==0xd0196
        assert [scalar(support_unit+0x284+n) for n in [0,4,8,12]]==[position[0]*32,position[1]*32,height+position[0]*32*slope,0.25]
        assert [scalar(tick_path+n) for n in [0x1c,0x20,0x24,0x28]]==[completion_sentinel]*4
        assert read(tick_path+0x88)[0]&0x100000
        assert read(support_mover+0x9c,2)==([5,201] if accept_next else [0xffffffff]*2)
        assert [scalar(support_mover+n) for n in [0x78,0x7c,0x80,0x84]]==[*position,0,0]
        assert support_trace.count(0x6f5fa7a0)==1
        assert support_trace.count(0x6f69a840)==1
        assert support_trace.count(0x6f171340)==1+int(accept_next)
        assert support_trace.count(0x6f684480)==1
        assert support_trace.count(0x6f73c800)==2
        assert support_trace.count(0x6f745110)==8
        assert not any(a in support_trace for a in [0x6f170d50,0x6f170dc0])
        assert read(move_ability+0x20)[0]==4*int(accept_next)
        assert read(support_unit+0x174,2)==([3,103] if accept_next else [0xffffffff]*2)
        assert read(support_unit+0x19c,2)==[0xffffffff]*2
        for address in [0x6f691260,0x6f67df00,0x6f67de20,0x6f694a10]:
            expected_count=int(active_task)+int(reject_next and address==0x6f691260)-int(accept_next and address in [0x6f67de20,0x6f694a10])
            assert support_trace.count(address)==expected_count,(hex(address),support_trace)
        assert support_trace.count(0x6f5ffb60)==int(has_next)
        assert support_trace.count(0x6f05b440)==int(has_next)
        assert support_trace.count(0x6f05b970)==int(accept_next)
        for address in [0x6f5fc900,0x6f169910,0x6f169840,0x6f16bcf0,0x6f169c50,0x6f169d60,0x6f16b7b0,0x6f16de50,0x6f169a30]:
            assert support_trace.count(address)==int(accept_next),(hex(address),support_trace)
        if active_task:
            assert bytes(machine.mem_read(arrival_task,0x50))==arrival_task_before
            request=arrival_release_header+4
            assert read(arrival_task_wrapper+0x20)[0]==request
            assert read(tick_slots+20)[0]==arrival_task_wrapper
            assert read(request+0xc,5)==[arrival_release_clock,0x20000,1,arrival_task_wrapper,0]
            assert scalar(request+4)==time_deadzone and scalar(request+8)==time_deadzone
            assert read(arrival_release_clock+0x38,2)==[0,1+int(reject_next)]
            assert read(arrival_release_clock+0x20)[0]==2+int(reject_next)
            assert read(arrival_release_heap+4)[0]==request
            assert read(support_unit+0x18c,2)==[0,0]
            machine.reg_write(UC_X86_REG_EDX,102)
            run(0x6f061320,2)
            assert machine.reg_read(UC_X86_REG_EAX)==0
            run(0x6f0557b0,arrival_task)
            assert read(arrival_release_clock+0x50)[0]==1+int(reject_next)
            if reject_next:
                next_request=arrival_release_header+0x44
                assert read(next_task_wrapper+0x20)[0]==next_request
                assert read(next_request+0xc,5)==[arrival_release_clock,0x20000,2,next_task_wrapper,0]
                assert read(subscriber_node+0x48)[0]==move_ability
                assert read(support_unit+0x5c)[0]==unit_status|1
                arrival_next_task_rejection_cases+=1
            elif accept_next:
                target=[position[0]+goal_offset,position[1]]
                assert read(next_task_wrapper+0x20)[0]==0
                assert read(support_unit+0x5c)[0]==unit_status
                assert scalar(support_mover+0x88)==8  # Original producer256 / world scale32.
                assert read(accepted_group+0x14,2)==[5,201]
                assert read(accepted_path+0x14,2)==[6,202]
                assert read(accepted_generator+0x14,2)==[0xffffffff]*2
                assert read(accepted_group+0x3c)[0]==accepted_path
                assert read(accepted_group+0x38)[0]==1
                assert read(accepted_members,2)==[0,100]
                assert read(accepted_members+0x14)[0]==support_mover
                assert [scalar(accepted_group+n) for n in [0x4c,0x50]]==target
                assert [scalar(accepted_group+n) for n in [0x54,0x58]]==list(position)
                assert [scalar(accepted_path+n) for n in [0x1c,0x20,0x24,0x28,0x2c,0x30]]==target*3
                assert read(accepted_path+0x50)[0]==read(accepted_path+0x70)[0]==0
                assert read(accepted_path+0x74,2)==[0xffffffff]*2
                assert read(accepted_path+0x84,4)==[(5000<<16)|700,0x600000 if adaptive else 0x400000,0,0]
                assert read(owner+0x3b8)[0]==accepted_group
                assert read(accepted_group+4,2)==[owner+0x3b0,0]
                assert read(tick_registry+0x40,5)==[4,0,6+int(adaptive),0,203]
                assert read(tick_slots+32,6)==[0xffffffff,0,0xfffffffe,accepted_group,0xfffffffe,accepted_path]
                for pool_offset,free_head,live in [(0x638,accepted_generator-4,0),(0x678,0,1),(0x958,0,1)]:
                    assert read(owner+pool_offset+0x14,3)==[free_head,live,1]
                assert read(actor_objects[0][1]+0x40)[0]==0  # Temporary occupancy exclusion restored.
                assert read(0x6fd3cce4,5)==[16,2,2,0,0]
                assert read(accepted_nodes+4,3)==[0x40190065,move_ability,0xd0196]
                assert read(accepted_nodes+0x14,3)==[0x40190066,move_ability,0xd0198]
                assert read(subscriber_node+0x48)[0]==move_ability
                arrival_next_task_acceptance_cases+=int(not stock_ui)
                # Empty backing storage only: the next tick must build its own routes.
                nodes,search_heap,member_route,group_route,member_coarse=[0x10400000+n for n in [0,0x30000,0x60000,0x64000,0x68000]]
                write(fine_system+0x1c,maps[1],1)
                write(fine_system+0x30,nodes)
                write(fine_system+0x3c,4096,0)
                write(fine_system+0x50,search_heap)
                write(fine_system+0x5c,32768,1,-3,100000,0)
                for path_ptr,route_data in [(tick_path,member_route),(accepted_path,group_route)]:
                    write(path_ptr+0x40,route_data)
                    write(path_ptr+0x4c,1024,0)
                    write(path_ptr+0x60,member_coarse if path_ptr==tick_path else group_route+0x2000)
                    write(path_ptr+0x6c,1024,0)
                write(tick_path+0x84,700|(400<<16))
                write(tick_path+0x9c,0x02000000)
                for row in range(16):
                    for kind,(limit,reload,budget) in enumerate([(5000,3,800),(2000,2,300),(400,2,900),(700,1,1100)]):
                        write(0x6fd53a90+row*0x70+kind*0x1c,limit|(reload<<16),budget,0,0,0,0,0)
                write(owner+0x538,100)
                floats(accepted_profile_buckets+0x100,0.5)
                run(0x6f05c8c0,support_unit+0x164,accepted_profile_buckets+0x100)
                run(0x6f05c890,support_unit+0x164,accepted_profile_buckets+0x100)
                if adaptive:
                    machine.mem_write(acc_system,bytes(0x400))
                    adaptive_maps=[0x10470000+i*0x100 for i in range(4)]
                    for level,desc in enumerate(adaptive_maps):
                        side=8>>level
                        storage=0x10480000+level*0x10000
                        machine.mem_write(desc,bytes(0x100))
                        machine.mem_write(storage,bytes(side*side*8))
                        write(desc+0x28,storage)
                        write(desc+0x3c,side,side)
                        floats(desc+0x64,2<<level,1/(2<<level))
                    write(owner+0x23c,*adaptive_maps)
                    write(acc_system+0x1c,*adaptive_maps)
                    write(acc_system+0x5c,0x104c0000)
                    write(acc_system+0x68,4096,0)
                    write(acc_system+0x7c,0x10500000)
                    write(acc_system+0x88,65536,0)
                    run(0x6f15d360,owner,0,0)
                    expected_levels=[]
                    for level,desc in enumerate(adaptive_maps):
                        side=8>>level
                        expected=[]
                        for y in range(side):
                            for x in range(side):
                                child_states=[int((2*x+dx,2*y+dy) in blocked) if level==0 else expected_levels[-1][(2*y+dy)*(side*2)+2*x+dx]
                                              for dy,dx in itertools.product(range(2),repeat=2)]
                                expected.append(0 if all(v==0 for v in child_states) else 1 if all(v==1 for v in child_states) else 2)
                        assert [read(read(desc+0x28)[0]+8*k+4)[0] for k in range(side*side)]==[v<<30 for v in expected]
                        expected_levels.append(expected)
                    assert read(accepted_path+0x88)[0]&0x200000
                    adaptive_requests.clear()
                def owner_step(phase):
                    before=[read(0x6fd53a90+k*0x1c+8,2) for k in range(64)]
                    old_tick,old_parity=read(owner+0x538,2)
                    old_heading=read(support_mover+0x8c)[0]
                    old_visual=read(support_mover+0xc8,2)
                    owner_events.clear()
                    if pair_ready:active_events.clear()
                    run(0x6f15aa80,owner)
                    if pair_ready:verify_active_pair(phase)
                    assert read(owner+0x538,2)==[old_tick+1,1-old_parity]
                    assert owner_events[:2]==[0x6f15aa80,0x6f167310]
                    assert owner_events.count(0x6f1702f0)==int(pair_ready)
                    assert read(owner+0x38c)[0]==0
                    if not pair_ready:assert read(owner+0x51c)[0]==0
                    if 0x6f16c150 in owner_events and 0x6f1705c0 in owner_events:
                        assert owner_events.index(0x6f16c150)<owner_events.index(0x6f1705c0)
                    for k,(work,countdown) in enumerate(before):
                        reload=[3,2,2,1][k%4]
                        expected=[work,countdown-1] if countdown else [0,reload]
                        if phase=='fresh' and k<4:expected[0]+=[9,0,9,37][k]
                        assert read(0x6fd53a90+k*0x1c+8,2)==expected,(phase,k,before[k],expected,read(0x6fd53a90+k*0x1c+8,2))
                    visual=read(support_mover+0xc8,2)
                    assert 0<=scalar(support_mover+0xc8)<scalar(0x6fcd5464)
                    assert abs(scalar(support_mover+0xcc))<=scalar(0x6fd541e4)
                    owner_frames.append(dict(phase=phase,owner_tick=old_tick+1,parity=1-old_parity,
                        events=list(map(hex,owner_events)),desired_before=old_heading,desired_after=read(support_mover+0x8c)[0],
                        visual_before=old_visual,visual_after=visual,visual_list=read(owner+0x440)[0],
                        scheduler_work=[read(0x6fd53a90+k*0x1c+8)[0] for k in range(4)]))
                support_trace.clear()
                if whole_owner:
                    assert read(owner+0x440)[0]==support_mover
                    assert read(support_mover+4,2)==[owner+0x438,0]
                    assert owner_events.count(0x6f170800)==1
                    owner_step('fresh')
                else:run(0x6f16c150,accepted_group)
                assert support_trace==[0x6f16c150,0x6f16ce10,0x6f165ae0,0x6f167ce0,0x6f148100,0x6f14a4c0]
                initial_route=[scalar(member_route+k*4) for k in range(read(tick_path+0x50)[0]*2)]
                if adaptive:
                    expected_coarse=[4,2,3.75,.75,2.75,.75,2.75,1.75,2,2]
                    assert len(adaptive_requests)==2
                    for request,kind,budget,route_ptr in zip(adaptive_requests,['group','member'],[5000,400],[accepted_path+0x54,tick_path+0x54]):
                        assert request==dict(lane=0,path=kind,route_struct=route_ptr,source=[2,2],goal=[4,2],
                            budget=budget,size_input=0,allow_partial=1,result=1,popped=9,route=expected_coarse)
                    assert read(tick_path+0x70,8)==[5,7,0,0,0,700|(400<<16),0x2200000,0]
                    assert read(accepted_path+0x70,8)==[5,0xffffffff,0,0,0,700|(5000<<16),0x600000,0]
                    assert [read(0x6fd53a90+k*0x1c+8)[0] for k in range(4)]==[9,0,9,37]
                    assert initial_route==obstacle_arrival_cases[0]['route']
                elif obstacle:
                    assert initial_route==[8,4,7.5,3.5,7.5,2.5,7.5,1.5,6.5,1.5,5.5,1.5,5.5,2.5,5.5,3.5,4,4]
                    assert read(fine_system+0x6c)[0]==37
                    assert read(fine_system+0xa0,2)==[0,0x02000000]
                    assert read(0x6fd53a90+0x54,7)==[700|(1<<16),1100,37,0,0,0,0]
                    assert read(tick_path+0x74,7)==[7,0,0,0,700|(400<<16),0x2000000,0]
                    route_cells=[tuple(math.floor(v) for v in initial_route[k:k+2]) for k in range(0,len(initial_route),2)]
                    deltas=[(b[0]-a[0],b[1]-a[1]) for a,b in zip(route_cells,route_cells[1:])]
                    assert all(max(abs(x),abs(y))==1 for x,y in deltas)
                    route_cost=sum(21 if x and y else 15 for x,y in deltas)
                    assert route_cost==reference_grid(blocked,16,16,tuple(map(math.floor,position)),tuple(map(math.floor,target)),0)[0]==132
                    assert not set(route_cells)&blocked
                else:
                    expected_route=target+[math.floor(position[0])+1.5,math.floor(position[1])+0.5]+list(position)
                    assert read(tick_path+0x50)[0]==3
                    assert [scalar(member_route+k*4) for k in range(6)]==expected_route
                    assert read(fine_system+0x6c)[0]==3
                    assert read(0x6fd53a90+0x54,7)==[700|(1<<16),1100,3,0,0,0,0]
                    assert read(tick_path+0x74,7)==[0,0,0,0,700|(400<<16),0x2000000,0]
                if not adaptive:
                    assert read(accepted_path+0x70,8)==[1,0xffffffff,0,0,0,700|(5000<<16),0x400000,0]
                    assert [scalar(group_route+0x2000+k*4) for k in range(2)]==[v/2 for v in target]
                assert read(accepted_group+0x5c,3)==[0,1,0]  # Fresh-route advance resets age.
                assert read(accepted_group+0x80)[0]==0x20000
                assert read(accepted_group+0x38)[0]==1
                assert read(support_mover+0x9c,2)==[5,201]
                assert [scalar(support_mover+n) for n in [0x78,0x7c]]==list(position)
                if not obstacle:
                    assert read(support_mover+0x80,2)==[0x40ffffff,0x80000000]
                assert scalar(accepted_members+0x20)==8
                assert read(accepted_members+0x28)[0]==0
                assert read(support_unit+0x174,2)==[3,103]
                assert read(move_ability+0x20)[0]==4 and read(move_ability+4)[0]==4
                assert read(subscriber_table)[0]==0x30400
                assert read(actor_objects[0][1]+0x40)[0]==0x20000000
                assert read(0)[0]==0
                arrival_fresh_tick_cases+=int(not stock_ui)
                # Actual simulation-clock advancement drains the completed task.
                class_bucket,class_entry,wrapper_pool,region_buffer,elapsed_ptr=[0x10311000+n for n in [0,0x100,0x200,0x300,0x400]]
                write(elapsed_ptr,0x74736b2e)
                run(0x6f198420,elapsed_ptr)
                write(bridge_host+0x28,class_bucket,0,0)
                write(class_bucket,0,0,class_entry)
                write(class_entry+4,machine.reg_read(UC_X86_REG_EAX))
                write(class_entry+0x18,0x74736b2e)
                write(class_entry+0x70,0x6fd70f1c)
                write(bridge_host+0x34,wrapper_pool)
                write(wrapper_pool+0x14,0,3)
                write(owner+0x254,0x6f04d9c0)
                write(owner+0x210,region_buffer)
                write(owner+0x230,0)
                machine.mem_write(region_buffer,bytes(0x40))
                # Original cache lookup/setters: controlled zero limits or authored hfoo UI.
                ui_bucket,ui_row=0x10311600,0x10311700
                write(0x6fd6a698,ui_bucket)
                write(0x6fd6a6a0,0)
                write(ui_bucket,0,0,ui_row)
                write(elapsed_ptr,0x68666f6f)
                run(0x6f198420,elapsed_ptr)
                write(ui_row+4,machine.reg_read(UC_X86_REG_EAX))
                write(ui_row+0x18,0x68666f6f)
                # Stock Footman UnitUI: maxPitch/maxRoll10 degrees, elevRad20.
                # Match the loader's x87 expression then float32 store; setters are original.
                ui_limit=10*scalar(0x6fa8af08)/scalar(0x6fa8af0c) if stock_ui else 0
                run(0x6f352db0,0x68666f6f,float_bits(ui_limit))
                run(0x6f352dd0,0x68666f6f,float_bits(ui_limit))
                run(0x6f352cf0,0x68666f6f,float_bits(20 if stock_ui else 0))
                assert read(ui_row+0x98,2)==[float_bits(ui_limit)]*2
                assert scalar(ui_row+0xa4)==(20 if stock_ui else 0)
                # Native mode0 samples a prebuilt height grid, not the mode−1 corners.
                height_layer,height_samples=0x10311900,0x10312000
                write(terrain+0x79c,height_layer)
                write(height_layer,16,16)
                floats(height_layer+8,32,32)
                write(height_layer+0x18,height_samples)
                floats(height_samples,*[height+(x+0.5)*32*slope for y in range(16) for x in range(16)])
                floats(arrival_release_clock+0x48,8)
                floats(elapsed_ptr,.005 if primary_route else 1/32)
                support_trace.clear()
                presentation_trace.clear()
                presentation_samples.clear()
                presentation_angles.clear()
                obstacle_steps=[]
                adaptive_indices=[]
                active_pair_rows=[]
                if active_pair:
                    sep_nodes,second_mover,second_path=setup_separation_pair((5.5,4))
                    pair_ready=True
                    active_events=[]
                    pair_snapshot={}
                    def observe_active_pair(uc,address,size,data):
                        event=dict(address=hex(address),self=uc.reg_read(UC_X86_REG_ECX))
                        if address==0x6f1702f0:
                            selected=sep_nodes.index(event['self'])
                            pair_snapshot.clear()
                            pair_snapshot.update(selected=selected,positions=[read(m+0x78,2) for m in [support_mover,second_mover]],
                                vectors=[read(q+0x18,2) for q in sep_nodes],words=[read(q+0x20)[0] for q in sep_nodes],
                                mover_c0=[read(m+0xc0)[0] for m in [support_mover,second_mover]],
                                occupancy_flags=[read(objs[1]+0x40)[0] for objs in actor_objects],fine_mode=read(fine_system+0xd4)[0])
                        if address==0x6f16ee80:event['endpoint_bits']=read(read(uc.reg_read(UC_X86_REG_ESP)+4)[0],2)
                        if address==0x6f17002e:event['accepted']=uc.reg_read(UC_X86_REG_EAX)
                        active_events.append(event)
                    active_addresses=[0x6f15aa80,0x6f167310,0x6f16c150,0x6f1705c0,0x6f1702f0,0x6f16ee80,0x6f17002e,
                        0x6f05c820,0x6f1603d0,0x6f16fa00,0x6f170960,0x6f16f570]
                    active_hooks=[machine.hook_add(UC_HOOK_CODE,observe_active_pair,begin=a,end=a) for a in active_addresses]
                    def verify_active_pair(phase):
                        snap=pair_snapshot
                        selected=snap['selected'];other=1-selected
                        assert selected==1-read(owner+0x53c)[0]
                        addresses=[int(e['address'],16) for e in active_events]
                        assert addresses[:2]==[0x6f15aa80,0x6f167310]
                        for a in [0x6f16c150,0x6f1705c0]:
                            if a in addresses:assert addresses.index(a)<addresses.index(0x6f1702f0)
                        sep_index=addresses.index(0x6f1702f0)
                        sep_addresses=addresses[sep_index:]
                        attempts=[e for e in active_events if e['address']==hex(0x6f16ee80)]
                        results=[e['accepted'] for e in active_events if e['address']==hex(0x6f17002e)]
                        old_positions=snap['positions'];old_vectors=snap['vectors']
                        old_word=snap['words'][selected]
                        cooldown=old_word&0xffff
                        speed=struct.unpack('<f',struct.pack('<I',snap['mover_c0'][selected]))[0]
                        expected_word=old_word
                        expected_position=old_positions[selected]
                        expected_vector=old_vectors[selected]
                        model_error=0
                        query_count=None
                        if cooldown:
                            expected_word=old_word-1
                            branch='cooldown'
                        elif speed>0:
                            expected_word=(old_word&0xffff0000)|7
                            expected_vector=[0,0]
                            branch='positive_c0'
                        else:
                            branch='accumulate'
                            proposed=[float_add(p,v) for p,v in zip(old_positions[selected],old_vectors[selected])]
                            point=[struct.unpack('<f',struct.pack('<I',v))[0] for v in proposed]
                            admissible=(math.floor(point[0]),math.floor(point[1])) not in blocked
                            if any(old_vectors[selected]):
                                assert len(attempts)==1 and attempts[0]['endpoint_bits']==proposed
                                assert results==[int(admissible)]
                                if admissible:expected_position=[float_add(p,float_subtract(q,p)) for p,q in zip(old_positions[selected],proposed)]
                            else:assert attempts==results==[]
                            assert sep_addresses.count(0x6f1603d0)==int(bool(attempts) and admissible)
                            assert sep_addresses.count(0x6f16fa00)==int(bool(attempts) and admissible)
                            current=[[scalar(m+0x78+k*4) for k in range(2)] for m in [support_mover,second_mover]]
                            query_count=int(snap['mover_c0'][other]==0)
                            assert read(owner+0x540+0x1c)[0]==query_count
                            if query_count:assert read(0x10511000)[0]==actor_objects[other][0]
                            accumulated=[struct.unpack('<f',struct.pack('<I',v))[0] for v in old_vectors[selected]]
                            config=[scalar(0x6fd54398+k*4) for k in range(5)]
                            if query_count:
                                delta=[a-b for a,b in zip(current[selected],current[other])]
                                distance=math.hypot(*delta)
                                assert distance>0.001
                                weight=config[3]*max(0,1-distance/config[0])**2
                                accumulated=[v+d*weight/distance for v,d in zip(accumulated,delta)]
                            magnitude=math.hypot(*accumulated)
                            length=min(magnitude*config[4],config[2])
                            wanted=[v*length/magnitude for v in accumulated] if length>=config[1] else [0,0]
                            if length<config[1]:expected_word=(old_word&0xffff0000)|7
                            actual=[scalar(sep_nodes[selected]+0x18+k*4) for k in range(2)]
                            model_error=max(abs(a-b) for a,b in zip(actual,wanted))
                            assert model_error<0.0002,(phase,actual,wanted,model_error)
                            expected_vector=read(sep_nodes[selected]+0x18,2)
                        if branch!='accumulate':
                            assert attempts==results==[]
                            assert 0x6f170960 not in sep_addresses
                        assert read(sep_nodes[selected]+0x20)[0]==expected_word
                        assert read(sep_nodes[selected]+0x18,2)==expected_vector
                        assert read(sep_nodes[other]+0x18,3)==old_vectors[other]+[snap['words'][other]]
                        assert read([support_mover,second_mover][selected]+0x78,2)==expected_position
                        assert read([support_mover,second_mover][other]+0x78,2)==old_positions[other]
                        assert [read(objs[1]+0x40)[0] for objs in actor_objects]==snap['occupancy_flags']
                        assert read(fine_system+0xd4)[0]==snap['fine_mode']
                        assert read(owner+0x230)[0]==0
                        for actor,objs in zip([support_mover,second_mover],actor_objects):
                            px,py=[scalar(actor+0x78+k*4) for k in range(2)]
                            for obj,grid,data,records in zip(objs,maps,cells,links):
                                rect=read(obj+0x1c,4)
                                want=([math.floor(py-.25),math.floor(px-.25),math.floor(py+.25)+1,math.floor(px+.25)+1]
                                    if obj==objs[0] else [math.floor(py),math.floor(px),math.floor(py)+1,math.floor(px)+1])
                                assert rect==want
                                expected_cells={(x,y) for y in range(rect[0],rect[2]) for x in range(rect[1],rect[3])}
                                actual_cells=set()
                                for cell in range(256):
                                    link=read(data+cell*4)[0]&0xffffff
                                    while link!=0xffffff:
                                        packed,payload=read(records+link*8,2)
                                        if payload==obj:
                                            if packed>>24==1:actual_cells.add((cell%16,cell//16))
                                            break
                                        link=packed&0xffffff
                                assert actual_cells==expected_cells
                        active_pair_rows.append(dict(phase=phase,owner_tick=read(owner+0x538)[0],branch=branch,
                            snapshot=dict(snap),events=list(active_events),attempted=bool(attempts),accepted=results==[1],
                            candidates=query_count,model_error=model_error,positions=[read(m+0x78,2) for m in [support_mover,second_mover]],
                            vectors=[read(q+0x18,3) for q in sep_nodes]))
                initial_motion={hex(offset):read(support_mover+offset)[0] for offset in (0x88,0x8c,0x90,0xb0,0xb4,0xb8,0xbc,0xc0,0xc4)}
                primary_steps=[]
                def advance_route_clock():
                    for primary_phase in range(6 if primary_route else 1):
                        machine.reg_write(UC_X86_REG_EDX,arrival_release_clock)
                        run(0x6f054190,elapsed_ptr)
                for elapsed_tick in range(1,129):
                    advance_route_clock()
                    old_position=[scalar(support_mover+0x78+k*4) for k in range(2)]
                    old_velocity=[scalar(support_mover+0x80+k*4) for k in range(2)]
                    if primary_route:
                        run(0x6f161040,support_mover,elapsed_ptr+4)
                        interval=read(elapsed_ptr+4)[0]
                    else:interval=float_bits(1/32)
                    if active_pair:active_events.clear()
                    if whole_owner:owner_step('elapsed')
                    else:run(0x6f16c150,accepted_group)
                    expected_position=[float_add(float_bits(v),float_multiply(float_bits(w),interval)) for v,w in zip(old_position,old_velocity)]
                    actual_position=[scalar(support_mover+0x78+k*4) for k in range(2)]
                    assert (pair_snapshot['positions'][0] if active_pair else read(support_mover+0x78,2))==expected_position,(position,elapsed_tick,actual_position,expected_position)

                    if primary_route:
                        assert read(support_mover+0x70,2)==read(arrival_release_clock+0x40,2)
                        primary_steps.append(dict(tick=elapsed_tick,clock=read(arrival_release_clock+0x40,3),elapsed=interval,
                            position_bits=read(support_mover+0x78,2),velocity_bits=read(support_mover+0x80,2),
                            heading_bits=read(support_mover+0x8c)[0],waypoint=read(tick_path+0x74)[0]))
                    else:
                        assert scalar(arrival_release_clock+0x40)==elapsed_tick/32
                        assert scalar(support_mover+0x70)==elapsed_tick/32
                    assert read(0x6fd70f20+8)[0]==1  # Old task reclaimed by actual clock drain.
                    for obj,grid,data,records in zip(actor_objects[0],maps,cells,links):
                        rect=read(obj+0x1c,4)
                        px,py=actual_position
                        expected_rect=([math.floor(py-.25),math.floor(px-.25),math.floor(py+.25)+1,math.floor(px+.25)+1]
                                       if obj==actor_objects[0][0] else [math.floor(py),math.floor(px),math.floor(py)+1,math.floor(px)+1])
                        assert rect==expected_rect
                        expected_cells={(x,y) for y in range(rect[0],rect[2]) for x in range(rect[1],rect[3])}
                        actual_cells=set()
                        for cell in range(256):
                            link=read(data+cell*4)[0]&0xffffff
                            while link!=0xffffff:
                                packed,payload=read(records+link*8,2)
                                if payload==obj:
                                    if packed>>24==1:actual_cells.add((cell%16,cell//16))
                                    break  # Newest insertion/removal supersedes older lazy records.
                                link=packed&0xffffff
                        assert actual_cells==expected_cells,(elapsed_tick,hex(obj),rect,actual_cells,expected_cells,read(obj+0x34,4),read(grid+0x88))
                    if adaptive:adaptive_indices.append([read(accepted_path+0x78)[0],read(tick_path+0x78)[0]])
                    if obstacle:
                        witness=min((swept_cell_witness(old_position,actual_position,cell) for cell in sorted(blocked)),key=lambda row:row['distance'])
                        clearance=witness['distance']
                        assert clearance>0,(elapsed_tick,old_position,actual_position,clearance)
                        assert (math.floor(actual_position[0]),math.floor(actual_position[1])) not in blocked
                        assert all(read(cells[1]+(y*16+x)*4)[0]&0x02000000 for x,y in blocked)
                        obstacle_steps.append(dict(tick=elapsed_tick,start_bits=[float_bits(v) for v in old_position],position_bits=read(support_mover+0x78,2),
                            velocity_bits=read(support_mover+0x80,2),heading_bits=read(support_mover+0x8c)[0],waypoint=read(tick_path+0x74)[0],clearance=clearance,nearest=witness))
                    if read(support_unit+0x174)[0]==0xffffffff:break
                else:raise AssertionError('real elapsed trajectory did not arrive')
                if adaptive:
                    if not primary_route:
                        assert elapsed_tick==34
                        assert obstacle_steps==obstacle_arrival_cases[0]['steps']
                    assert adaptive_indices==[[0,0]]*(elapsed_tick-1)+[[0,0xffffffff]]
                    assert len(adaptive_requests)==2  # No later accelerated replan.
                    if not whole_owner:assert [read(0x6fd53a90+k*0x1c+8)[0] for k in range(4)]==[9,0,9,37]
                elif obstacle:
                    assert elapsed_tick==34
                    assert [row['tick'] for row in obstacle_steps if row['clearance']<0.25]==[18,19]
                    assert abs(min(row['clearance'] for row in obstacle_steps)-0.22356081089800178)<1e-12
                    consumed=[key for key,rows in itertools.groupby(row['waypoint'] for row in obstacle_steps)]
                    assert consumed==[7,5,3,2,1,0,0xffffffff]
                    assert [row['tick'] for row in obstacle_steps if row['velocity_bits']==[0,0]]==[5,6,14,15,23,24,34]
                    assert sum((actual_position[k]-target[k])**2 for k in range(2))<0.49**2
                else:assert elapsed_tick==7
                assert read(support_unit+0x174,2)==[0xffffffff]*2
                assert read(support_unit+0x19c,2)==[0xffffffff]*2
                assert read(support_mover+0x80,2)==[0,0]
                assert read(move_ability+0x20)[0]==0
                assert support_trace.count(0x6f5fa7a0)==1
                assert support_trace.count(0x6f691260)==1
                assert support_trace.count(0x6f148100)==0
                assert [scalar(support_unit+0x284+k*4) for k in range(2)]==[v*32 for v in actual_position]
                assert abs(scalar(support_unit+0x28c)-(height+actual_position[0]*32*slope))<0.00001
                matrix=[scalar(support_unit+0x294+k*4) for k in range(9)]
                # Independent geometric reference: plane normal, perpendicular right,
                # then forward. All tested slopes remain below stock pitch/roll caps.
                angle=scalar(support_unit+0x290)  # Snapshot captured before owner visual update.
                normal_length=math.sqrt(1+slope*slope)
                normal=(-slope/normal_length,0,1/normal_length)
                right_raw=(-math.sin(angle),math.cos(angle),-slope*math.sin(angle))
                right_length=math.sqrt(sum(v*v for v in right_raw))
                right=tuple(v/right_length for v in right_raw)
                forward=(right[1]*normal[2]-right[2]*normal[1],
                         right[2]*normal[0]-right[0]*normal[2],
                         right[0]*normal[1]-right[1]*normal[0])
                expected_matrix=forward+right+normal
                matrix_error=max(abs(a-b) for a,b in zip(matrix,expected_matrix))
                assert matrix_error<0.000002,(matrix,expected_matrix,matrix_error)
                assert presentation_trace.count(0x6f66d990)==1
                assert presentation_trace.count(0x6f743810)==(3 if stock_ui else 0)
                assert presentation_trace.count(0x6f782a80)==(5 if stock_ui else 2)
                assert presentation_trace.count(int(resolved_crt_exports['_libm_sse2_sqrt_precise']['entry'],16))==(3 if stock_ui else 0)
                assert presentation_trace.count(0x6f1c7ba0)==int(stock_ui)
                assert presentation_trace.count(0x6f1c6c40)==int(stock_ui)
                if stock_ui:
                    center=[v*32 for v in actual_position]
                    expected_samples=[[center[0]-20*math.cos(angle),center[1]-20*math.sin(angle)]]
                    expected_samples.extend([[center[0]+20*math.cos(angle+delta),center[1]+20*math.sin(angle+delta)]
                                             for delta in [-math.pi/4,math.pi/4]])
                    assert len(presentation_samples)==3
                    assert max(abs(a-b) for row,want in zip(presentation_samples,expected_samples) for a,b in zip(row,want))<0.00005
                    assert len(presentation_angles)==1
                    assert all(abs(v)<=ui_limit for v in presentation_angles[0][1:])
                assert read(move_ability+4)[0]==2
                assert read(subscriber_table)[0]==0x10400
                assert read(accepted_nodes+8)[0]==read(accepted_nodes+0x18)[0]==0
                assert read(support_mover+0x9c,2)==[0xffffffff]*2
                assert read(actor_objects[0][1]+0x40)[0]==0
                assert read(tick_path+0x74,7)==[0xffffffff,0xffffffff,0,0,700|(400<<16),0x300000 if adaptive else 0x100000,0]
                assert read(next_task_wrapper+0x20)[0]==arrival_release_header+4
                advance_route_clock()
                assert read(0x6fd70f20+8)[0]==0
                assert read(arrival_release_clock+0x20)[0]==1
                assert read(next_task_wrapper+0x14,4)==[0xffffffff,0xffffffff,0,0]
                if whole_owner:owner_step('release')
                else:run(0x6f16c150,accepted_group)
                assert read(tick_registry+0x48)[0]==2+int(adaptive)  # Adaptive fixture also retains mover-owned path.
                assert read(owner+0x3b8)[0]==0
                assert read(accepted_group+0x38)[0]==0
                assert read(owner+0x678+0x14,3)==[accepted_group-4,0,1]
                assert read(owner+0x958+0x14,3)==[accepted_path-4,0,1]
                assert read(arrival_release_clock+0x38,2)==[arrival_release_header,0]
                assert read(0)[0]==0
                if adaptive:
                    assert read(tick_path+0x14,2)==[7,104]
                    assert read(tick_slots+7*8,2)==[0xfffffffe,tick_path]
                    assert read(support_mover+0xa8)[0]==tick_path
                    if whole_owner:
                        for settle_tick in range(1,33):
                            advance_route_clock()
                            owner_step('settle')
                            if read(owner+0x440)[0]==0:break
                        else:raise AssertionError('visual heading failed to settle')
                        if not primary_route:assert settle_tick==8 and len(owner_frames)==44
                        assert len(owner_frames)==elapsed_tick+settle_tick+2
                        assert read(owner+0x538,2)==[100+len(owner_frames),len(owner_frames)&1]
                        assert read(support_mover+4,2)==[0,0]
                        assert read(support_mover+0xc8,2)==[read(support_mover+0x8c)[0],0]
                        assert read(tick_registry+0x48)[0]==3
                        assert read(tick_slots+7*8,2)==[0xfffffffe,tick_path]
                        assert read(support_mover+0xa8)[0]==tick_path
                        owner_counts={hex(a):sum(row['events'].count(hex(a)) for row in owner_frames)
                                      for a in [0x6f15aa80,0x6f167310,0x6f16c150,0x6f1705c0,0x6f170cf0,0x6f1702f0]}
                        assert list(owner_counts.values())==[len(owner_frames),len(owner_frames),elapsed_tick+2,len(owner_frames),1,len(owner_frames)-1 if active_pair else 0]
                        assert any(row['visual_before']!=row['visual_after'] for row in owner_frames)
                        assert all(row['visual_list']==support_mover for row in owner_frames[:-1])
                        assert owner_frames[-1]['visual_list']==0
                    adaptive_row=dict(ticks=elapsed_tick,matrix_max_error=matrix_error,
                        start=position,goal=target,blocked=sorted(blocked),radius=.25,radius_world=8,
                        path_activation=['1657c0','166060'],retained_member_path_identity=[7,104],
                        hierarchy_builder='15d360',hierarchy_sides=[8,4,2,1],hierarchy_lane0_states=expected_levels,
                        requests=[dict(request) for request in adaptive_requests],fine_route=initial_route,
                        fine_expansions=37,scheduler_work=[9,0,9,37],accelerated_indices=adaptive_indices,
                        same_trajectory_as_fine_only=True,steps=obstacle_steps,
                        boundaries='One admitted group and member adaptive request each; indices immediately0 for this near-goal case. Owner538 fixed100; periodic scheduler update, denials and replans not executed. Synthetic fine map and descriptors; original hierarchy classification executed.')
                    if whole_owner:
                        adaptive_row.update(owner_updates=len(owner_frames),settle_ticks=settle_tick,
                            clock_advances=elapsed_tick+1+settle_tick,scheduler_bucket_checks=64*len(owner_frames),
                            owner_frames=list(owner_frames),owner_entrypoint='15aa80',call_counts=owner_counts,
                            visual_table_initializer=['004200','004210'],visual_table_row0_bits=read(0x6fd541e0,4),
                            visual_threshold_bits=read(0x6fd542e0)[0],visual_list_insertions=1,
                            final_visual_bits=read(support_mover+0xc8,2),
                            presentation_order='Unit pose captures cached heading during group update; owner visual heading update follows in the same frame',
                            boundaries='Full owner singleton: actual scheduler, group, visual-facing update and final virtual58 unlink. Shared38c and separation51c lists empty; no populated repulsion pass. Two admitted adaptive requests, no denial or replan. Synthetic map descriptors with original hierarchy classification. Retained registered path7/104 belongs to live mover after group/path reclamation')
                        if active_pair:
                            for hook in active_hooks:machine.hook_del(hook)
                            assert len(active_pair_rows)==43
                            assert sum(row['branch']=='positive_c0' for row in active_pair_rows)==2
                            assert sum(row['branch']=='cooldown' for row in active_pair_rows)==33
                            assert sum(row['branch']=='accumulate' for row in active_pair_rows)==8
                            assert sum(row['attempted'] for row in active_pair_rows)==4
                            assert sum(row['accepted'] for row in active_pair_rows)==4
                            assert [row['owner_tick'] for row in active_pair_rows if row['accepted']]==[137,139,141,143]
                            assert all(not row['attempted'] for row in active_pair_rows if row['phase']=='elapsed')
                            assert read(support_mover+0x78,2)!=obstacle_steps[-1]['position_bits']
                            assert read(second_mover+0x78,2)==[float_bits(5.5),float_bits(4)]
                            assert read(tick_registry+0x48)[0]==3
                            adaptive_row.update(separation_rows=active_pair_rows,separation_updates=43,
                                separation_branch_counts=dict(cooldown=33,positive_c0=2,accumulate=8),
                                separation_attempts=4,separation_accepted=4,
                                separation_model_max_error=max(row['model_error'] for row in active_pair_rows),
                                separation_config_bits=read(0x6fd54398,5),
                                repulsor_start_bits=[float_bits(5.5),float_bits(4)],
                                mover_after_settling_bits=read(support_mover+0x78,2),
                                unit_arrival_pose_bits=read(support_unit+0x284,2),
                                profile='Controlled hfoo repulse1 after stock repulse0 predicate control; radius.25 and query mask02000000 controlled, not a stock ground or hgry movement profile',
                                boundaries='Full owner active group plus registered nearby repulsor. Positive mover+c0 clears its separation vector and excludes it as candidate; actual cooldown delays later visits. Detour and natural arrival remain bit-identical to singleton; four accepted post-arrival displacements follow. Second mover has controlled path/occupancy without unit payload or ability/task. No simultaneous push while c0 positive, dynamic-mask collision fidelity, populated shared groups or region payloads. Unit arrival pose is a snapshot before subsequent separation; later unit presentation synchronization not executed')
                            active_pair_cases.append(adaptive_row)
                            continue
                        if primary_route:
                            primary_arrival_cases.append(dict(start=position,goal=target,blocked=sorted(blocked),route=initial_route,
                                initial_motion=initial_motion,ticks=elapsed_tick,steps=primary_steps,owner_updates=len(owner_frames),
                                settle_ticks=settle_tick,call_counts=owner_counts))
                            continue
                        owner_arrival_cases.append(adaptive_row)
                        sep_nodes,second_mover,second_path=setup_separation_pair((9,4))
                        pair_events=[]
                        def observe_pair(uc,address,size,data):
                            event=dict(address=hex(address),self=uc.reg_read(UC_X86_REG_ECX))
                            if address==0x6f16ee80:
                                event['endpoint_bits']=read(read(uc.reg_read(UC_X86_REG_ESP)+4)[0],2)
                            if address==0x6f17002e:event['accepted']=uc.reg_read(UC_X86_REG_EAX)
                            pair_events.append(event)
                        pair_addresses=[0x6f15aa80,0x6f167310,0x6f1702f0,0x6f171320,0x6f16ffa0,
                            0x6f16ee80,0x6f17002e,0x6f05c820,0x6f15f7b0,0x6f1603d0,
                            0x6f16fa00,0x6f170960,0x6f16f570]
                        pair_hooks=[machine.hook_add(UC_HOOK_CODE,observe_pair,begin=a,end=a) for a in pair_addresses]
                        pair_rows=[]
                        actors_pair=[support_mover,second_mover]
                        config=[scalar(0x6fd54398+k*4) for k in range(5)]
                        pair_model_error=0
                        for pair_tick in range(16):
                            machine.reg_write(UC_X86_REG_EDX,arrival_release_clock)
                            run(0x6f054190,elapsed_ptr)
                            old_positions=[read(m+0x78,2) for m in actors_pair]
                            old_vectors=[read(q+0x18,2) for q in sep_nodes]
                            selected=pair_tick%2
                            other=1-selected
                            old_parity=read(owner+0x53c)[0]
                            old_mode=read(fine_system+0xd4)[0]
                            pair_events.clear()
                            run(0x6f15aa80,owner)
                            addresses=[int(e['address'],16) for e in pair_events]
                            assert addresses[:4]==[0x6f15aa80,0x6f167310,0x6f1702f0,0x6f171320]
                            assert pair_events[2]['self']==sep_nodes[selected]
                            assert read(owner+0x53c)[0]==1-old_parity
                            assert addresses.count(0x6f1702f0)==1
                            assert addresses.index(0x6f16ffa0)<addresses.index(0x6f170960)<addresses.index(0x6f16f570)
                            attempts=[e for e in pair_events if e['address']==hex(0x6f16ee80)]
                            results=[e['accepted'] for e in pair_events if e['address']==hex(0x6f17002e)]
                            proposed=[float_add(p,v) for p,v in zip(old_positions[selected],old_vectors[selected])]
                            point=[struct.unpack('<f',struct.pack('<I',v))[0] for v in proposed]
                            admissible=(math.floor(point[0]),math.floor(point[1])) not in blocked
                            if any(old_vectors[selected]):
                                assert len(attempts)==1 and attempts[0]['endpoint_bits']==proposed
                                assert results==[int(admissible)]
                            else:assert attempts==results==[]
                            expected_position=old_positions[selected]
                            if attempts and admissible:
                                expected_position=[float_add(p,float_subtract(q,p)) for p,q in zip(old_positions[selected],proposed)]
                            assert read(actors_pair[selected]+0x78,2)==expected_position
                            assert read(actors_pair[other]+0x78,2)==old_positions[other]
                            assert read(sep_nodes[other]+0x18,2)==old_vectors[other]
                            assert addresses.count(0x6f1603d0)==int(bool(attempts) and admissible)
                            assert addresses.count(0x6f16fa00)==int(bool(attempts) and admissible)
                            assert read(fine_system+0xd4)[0]==old_mode
                            assert all(read(objs[1]+0x40)[0]==0 for objs in actor_objects)
                            assert read(owner+0x540+0x1c)[0]==1
                            assert read(0x10511000)[0]==actor_objects[other][0]
                            current=[[scalar(m+0x78+k*4) for k in range(2)] for m in actors_pair]
                            delta=[a-b for a,b in zip(current[selected],current[other])]
                            distance=math.hypot(*delta)
                            weight=config[3]*max(0,1-distance/config[0])**2
                            previous=[struct.unpack('<f',struct.pack('<I',v))[0] for v in old_vectors[selected]]
                            accumulated=[v+d*weight/distance for v,d in zip(previous,delta)]
                            magnitude=math.hypot(*accumulated)
                            length=min(magnitude*config[4],config[2])
                            expected_vector=[v*length/magnitude for v in accumulated] if length>=config[1] else [0,0]
                            actual_vector=[scalar(sep_nodes[selected]+0x18+k*4) for k in range(2)]
                            error=max(abs(a-b) for a,b in zip(actual_vector,expected_vector))
                            pair_model_error=max(pair_model_error,error)
                            assert error<0.0002,(pair_tick,actual_vector,expected_vector,error)
                            for actor,objs in zip(actors_pair,actor_objects):
                                px,py=[scalar(actor+0x78+k*4) for k in range(2)]
                                for obj,grid,data,records in zip(objs,maps,cells,links):
                                    rect=read(obj+0x1c,4)
                                    expected_rect=([math.floor(py-.25),math.floor(px-.25),math.floor(py+.25)+1,math.floor(px+.25)+1]
                                        if obj==objs[0] else [math.floor(py),math.floor(px),math.floor(py)+1,math.floor(px)+1])
                                    assert rect==expected_rect
                                    expected_cells={(x,y) for y in range(rect[0],rect[2]) for x in range(rect[1],rect[3])}
                                    actual_cells=set()
                                    for cell in range(256):
                                        link=read(data+cell*4)[0]&0xffffff
                                        while link!=0xffffff:
                                            packed,payload=read(records+link*8,2)
                                            if payload==obj:
                                                if packed>>24==1:actual_cells.add((cell%16,cell//16))
                                                break
                                            link=packed&0xffffff
                                    assert actual_cells==expected_cells
                            pair_rows.append(dict(tick=pair_tick,selected=selected,parity=read(owner+0x53c)[0],
                                before_positions=old_positions,before_vectors=old_vectors,proposed_bits=proposed,
                                attempted=bool(attempts),accepted=bool(attempts) and admissible,
                                positions=[read(m+0x78,2) for m in actors_pair],vectors=[read(q+0x18,2) for q in sep_nodes],
                                candidate=other,model_error=error,events=list(pair_events)))
                        for hook in pair_hooks:machine.hook_del(hook)
                        assert sum(row['attempted'] for row in pair_rows)==14
                        assert sum(row['accepted'] for row in pair_rows)==10
                        assert [row['tick'] for row in pair_rows if row['attempted'] and not row['accepted']]==[8,10,12,14]
                        assert read(owner+0x538,2)==[160,0]
                        assert read(owner+0x7f8+0x18,2)==[2,2]
                        assert read(owner+0x230)[0]==0
                        owner_separation_cases.append(dict(ticks=16,attempts=14,accepted=10,blocked=4,
                            registration=['693d50','66fc50','1710e0','170820'],config_initializer='004790',
                            config_bits=read(0x6fd54398,5),profile='Stock hfoo repulse0 negative control; controlled hfoo repulse1 selector/category/rank0. Matches hgry separation fields only, not complete hgry movement profile',
                            radius_bits=float_bits(.25),radius_world=8,model_max_error=pair_model_error,steps=pair_rows,
                            boundaries='Post-arrival two-mover separation; first mover retains real completed ability/task history, second mover has controlled path/occupancy with no ability/task or unit payload. Recycled CPoSeparate pool, preallocated query storage. Native endpoint validator retains fine collision flags; proximity descriptor high exclusion byte clear. Original region callback executes with no region payloads. No simultaneous active group/visual passes, exact-overlap random branch or populated shared groups'))
                    else:adaptive_arrival_cases.append(adaptive_row)
                elif obstacle:
                    obstacle_arrival_cases.append(dict(blocked=sorted(blocked),start=position,goal=target,route=initial_route,initial_motion=initial_motion,
                        route_cost=route_cost,expanded_nodes=37,ticks=elapsed_tick,steps=obstacle_steps,
                        minimum_swept_clearance=min(row['clearance'] for row in obstacle_steps),
                        radius=0.25,radius_bits=float_bits(0.25),radius_world=8,
                        radius_provenance='Controlled mover+90=.25 fine units; original148100 receives radius pointer=.25 and derives fine-system+a0 sizeclass0. Path+9c mask02000000 controlled. Stock hfoo collision profile producer not executed; hfoo UI fields cover presentation only',
                        disc_overlap_ticks=[row['tick'] for row in obstacle_steps if row['clearance']<0.25],
                        collision_scope='Center sweep avoids blocked cells; measured disc overlap records quantized footprint behavior, not a strict Euclidean-radius guarantee',matrix_max_error=matrix_error))
                    if len(obstacle_arrival_cases)==2:assert obstacle_arrival_cases[0]==obstacle_arrival_cases[1]
                else:
                    (slope_arrival_cases if slope else stock_ui_arrival_cases if stock_ui else elapsed_arrival_cases).append(dict(slope=slope,matrix=matrix,matrix_max_error=matrix_error,terrain_samples=list(presentation_samples),start=position,height=height,subscriptions=subscriptions,unit_status=unit_status,ticks=elapsed_tick,position_bits=read(support_mover+0x78,2)))
            else:
                active_arrival_dispatch_cases+=1
        else:
            arrival_dispatch_cases+=1
    for hook in support_hooks: machine.hook_del(hook)
    all_elapsed_cases=elapsed_arrival_cases+stock_ui_arrival_cases+slope_arrival_cases+obstacle_arrival_cases+adaptive_arrival_cases+owner_arrival_cases+active_pair_cases
    # Independent retained-pose sequences: supplied constant .1 elapsed, not
    # a claim about owner-clock cadence. Each zero-elapsed velocity publication
    # is followed by complete original integration and scalar world projection.
    native_pose_sequences=[]
    scratch_a,scratch_b,scratch_out=system+0x2200,system+0x2210,system+0x2220
    def original_scalar(entry,left,right):
        write(scratch_a,left);write(scratch_b,right)
        machine.reg_write(UC_X86_REG_EDX,scratch_a)
        run(entry,scratch_out,scratch_b)
        return read(scratch_out)[0]
    def original_world(grid,origin):
        return [original_scalar(0x6f06fbb0,original_scalar(0x6f06f9c0,g,float_bits(32)),float_bits(o))
                for g,o in zip(grid,origin)]
    for position,origin,pattern in itertools.product([(1.125,1.75),(4.25,5.5),(8.03125,8.0625)],
                                                     [(0,0),(-256,-256),(-2048,512)],range(2)):
        machine.mem_write(owner,bytes(0x100));machine.mem_write(mover,bytes(0x1d0))
        write(mover,0x6fa9129c,owner+0x200,0)
        write(mover+0x14,0)
        clock=owner+0x14;write(clock+0x44,0);floats(clock+0x48,8)
        floats(mover+0x78,*position,0,0,100/32,.125)
        floats(mover+0x90,.25);write(mover+0x94,*objects)
        for obj,grid,data,records,bitmap in zip(objects,maps,cells,links,bitmaps):
            machine.mem_write(obj,bytes(0x80));write(obj+0x2c,grid);write(obj+0x34,0x01000001)
            machine.mem_write(grid,bytes(0x100));write(grid+0x28,data);write(grid+0x3c,16,16)
            write(grid+0x54,0,0,16,16);floats(grid+0x68,1);write(grid+0x78,records)
            write(grid+0x84,1024,0);write(grid+0x98,bitmap);write(grid+0xac,0xffffff)
            machine.mem_write(bitmap,bytes(32));write(data,*([0xffffff]*256))
        initial=read(mover+0x78,2)
        initial_world=original_world(initial,origin)
        steps=[]
        for tick in range(16):
            heading=.6 if pattern and tick&1 else .125
            # Reset both supplied clock words together before this independent
            # interval. Retained pose/velocity/spatial records are not reset.
            floats(clock+0x40,.1);floats(mover+0x70,.1);write(mover+0x74,0)
            floats(speed_ptr,100/32);floats(heading_ptr,heading)
            run(0x6f16fe20,mover,speed_ptr,heading_ptr)
            velocity=[original_scalar(0x6f06f9c0,w,float_bits(32)) for w in read(mover+0x80,2)]
            inputs=read(mover+0x78,2)+velocity+[float_bits(o) for o in origin]+[float_bits(.1)]
            before_world=original_world(inputs[:2],origin)
            direct_world=[original_scalar(0x6f06fbb0,w,original_scalar(0x6f06f9c0,v,float_bits(.1)))
                          for w,v in zip(before_world,velocity)]
            floats(clock+0x40,.2);floats(displacement,0,0)
            run(0x6f1603d0,mover,displacement)
            outputs=read(mover+0x78,2)
            outputs+=original_world(outputs,origin)
            if engine:
                actual=(ctypes.c_uint32*4)();supplied=(ctypes.c_uint32*7)(*inputs)
                engine.pathing_native_pose(supplied,actual)
                assert list(actual)==outputs,(position,origin,pattern,tick,list(actual),outputs)
                assert list(supplied)==inputs
            steps.append(dict(input=inputs,output=outputs,heading=float_bits(heading),
                              velocity=velocity,facing=read(mover+0x8c)[0],direct_world=direct_world))
        native_pose_sequences.append(dict(position=initial,world=initial_world,origin=[float_bits(o) for o in origin],
                                          pattern=pattern,steps=steps))
    assert len(native_pose_sequences)==18 and sum(len(s['steps']) for s in native_pose_sequences)==288
    assert sum(r['direct_world']!=r['output'][2:] for s in native_pose_sequences for r in s['steps'])==139
    # Complete world-position bridge writer with supplied clocks and retained
    # fine bits. Notification flag1 is real; unit virtual callbacks stay disabled.
    axis_bridge,axis_registry,axis_slots,axis_config,axis_point,axis_out=[system+n for n in (0x2400,0x2600,0x2700,0x2800,0x2900,0x2910)]
    write(0x6fd68610,axis_registry)
    write(axis_registry+0xc,axis_slots);write(axis_registry+0x1c,1)
    write(axis_registry+0x2c,axis_slots);write(axis_registry+0x3c,1)
    write(axis_slots,-2,mover);write(0x6fd3c82c,axis_config)
    position_cases=[]
    for seq,axis,mode,phase in itertools.product(native_pose_sequences,range(2),range(4),range(4)):
        source=seq['steps'][seq['pattern']]
        domain=0x80000000 if phase==3 else 0
        write(mover+0x14,domain,100);write(axis_bridge+8,domain,100)
        write(mover+0x78,*source['output'][:2])
        fine_velocity=[original_scalar(0x6f06f9c0,v,float_bits(1/32)) for v in source['velocity']]
        write(mover+0x80,*fine_velocity)
        write(mover+0x8c,source['facing'])
        write(axis_config+0x6c,*seq['origin'])
        axis_clock=owner+(0x68 if domain else 0x14)
        old_time,current_time,old_epoch,current_epoch=(7.875,0,0,1) if phase==2 else (0,0 if phase==0 else .125,0,0)
        floats(axis_clock+0x40,current_time);write(axis_clock+0x44,current_epoch);floats(axis_clock+0x48,8)
        floats(mover+0x70,old_time);write(mover+0x74,old_epoch)
        run(0x6f058900,axis_bridge,axis_out)
        queried=read(axis_out,3);requested=queried[:2]
        if mode==1:requested[axis]=original_scalar(0x6f06fbb0,requested[axis],float_bits(.125))
        elif mode in (2,3):requested[axis]+=(1 if not requested[axis]&0x80000000 else -1)*(1 if mode==2 else -1)
        inputs=read(mover+0x78,2)+source['velocity']+seq['origin']+requested+[float_bits(0 if phase==0 else .125)]
        prior=read(mover+0x80,4)
        write(axis_point,*requested,0)
        run(0x6f05c200,axis_bridge,axis_point,1)
        assert read(mover+0x80,4)==prior and read(mover+0x70,2)==[float_bits(current_time),current_epoch]
        outputs=read(mover+0x78,2)
        run(0x6f058900,axis_bridge,axis_out)
        outputs+=read(axis_out,2)
        if engine:
            actual=(ctypes.c_uint32*4)();supplied=(ctypes.c_uint32*9)(*inputs)
            engine.pathing_pose_write(supplied,actual)
            assert list(actual)==outputs,(seq['origin'],axis,mode,phase,list(actual),outputs)
            assert list(supplied)==inputs
        # Next supplied-.1 interval retains the setter's fine pose. Reset only
        # clock origins together, publish .6 heading, then integrate normally.
        floats(axis_clock+0x40,0);floats(mover+0x70,0);write(mover+0x74,current_epoch)
        floats(speed_ptr,100/32);floats(heading_ptr,.6)
        run(0x6f16fe20,mover,speed_ptr,heading_ptr)
        next_velocity=[original_scalar(0x6f06f9c0,v,float_bits(32)) for v in read(mover+0x80,2)]
        floats(axis_clock+0x40,.1);floats(displacement,0,0)
        run(0x6f1603d0,mover,displacement)
        next_output=read(mover+0x78,2)+original_world(read(mover+0x78,2),[struct.unpack('<f',struct.pack('<I',v))[0] for v in seq['origin']])
        if engine:
            velocity_words=(ctypes.c_uint32*6)(*source['velocity'],float_bits(100),float_bits(.6),float_bits(100),source['facing'])
            engine.pathing_velocity_world_commit(velocity_words)
            assert list(velocity_words)[:2]==next_velocity and velocity_words[5]==read(mover+0x8c)[0]
            next_input=outputs[:2]+next_velocity+seq['origin']+[float_bits(.1)]
            next_actual=(ctypes.c_uint32*4)()
            engine.pathing_native_pose((ctypes.c_uint32*7)(*next_input),next_actual)
            assert list(next_actual)==next_output
        position_cases.append(dict(input=inputs,output=outputs,queried=queried[:2],axis=axis,mode=mode,phase=phase,
                                   facing=source['facing'],next=next_output+next_velocity+read(mover+0x8c)))
    assert len(position_cases)==576
    if args.position_fixture:
        args.position_fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=position_cases,
            scope='Complete original05c200/058900 with supplied clocks, retained spatial records and notification flag1. Unit virtual callbacks disabled by original mover flags; public producers and clock owner cadence covered separately.'),separators=(',',':'))+'\n')
    # Actual primary producer04c1a0 advances timer clock, gameplay clock,
    # auxiliary callbacks and unit clock in that order. Empty original heaps
    # isolate the producer; owner phase is separately observed in owned retail.
    clock_memory=0x61000000
    machine.mem_map(clock_memory,0x4000)
    clock_config,clock_aux,clock_delta=clock_memory,clock_memory+0x1000,clock_memory+0x2000
    write(0x6fd3c82c,clock_config);write(clock_config+0x40,clock_aux)
    for clock in [clock_aux,owner+0x14,owner+0xbc,owner+0x164]:
        write(clock+0x20,1);floats(clock+0x40,0);write(clock+0x44,0);floats(clock+0x48,300);write(clock+0x4c,0)
    write(owner+0x14+0x4c,0x1000)
    write(mover+0x14,0,100);write(axis_bridge+8,0,100)
    write(mover+0x70,0,0);floats(mover+0x78,1.125,1.75);floats(mover+0x80,0,0)
    floats(mover+0x88,100/32);floats(mover+0x8c,.125);write(mover+0xd8,0)
    floats(clock_config+0x6c,-2048,512);floats(clock_delta,.005)
    clock_advances=[];clock_trajectory=[];clock_frames=[]
    for tick in range(1,6001):
        previous = read(owner+0x14+0x40,4)
        run(0x6f04c1a0,clock_delta)
        states=[read(c+0x40,4) for c in [clock_aux,owner+0x14,owner+0xbc,owner+0x164]]
        assert all(state[:3]==states[0][:3] for state in states)
        clock_advances.append(states[1][:3])
        if engine:
            result=(ctypes.c_uint32*4)()
            engine.pathing_clock_advance((ctypes.c_uint32*5)(*previous,float_bits(.005)),result)
            assert list(result)[:3]==states[1][:3] and result[3]==0
        if tick%6==0:
            before=read(mover+0x70,8)
            floats(speed_ptr,100/32);floats(heading_ptr,.125)
            run(0x6f16fe20,mover,speed_ptr,heading_ptr)
            velocity=[original_scalar(0x6f06f9c0,v,float_bits(32)) for v in read(mover+0x80,2)]
            run(0x6f058900,axis_bridge,axis_out)
            clock_trajectory.append(dict(tick=tick,clock=states[1][:3],before=before,
                after=read(mover+0x70,8),output=read(mover+0x78,2)+read(axis_out,2)+velocity+read(mover+0x8c)))
        if tick%20==0:
            run(0x6f058900,axis_bridge,axis_out)
            clock_frames.append(dict(tick=tick,clock=states[1][:3],state=read(mover+0x70,8),
                output=read(mover+0x78,2)+read(axis_out,2)+velocity+read(mover+0x8c)))
    assert len(clock_advances)==6000 and len(clock_trajectory)==1000 and len(clock_frames)==300
    clock_controls=[]
    clock=owner+0x14
    for span,time,epoch,flags,increment in itertools.product([8,300],
            [0,-.125,.125,7.999999,8,8.000001,299.99999,300,300.00003],
            [0,1,0xffffffff],[0,1,0x1000,0x1001],
            [0,-0.0,.005,-.005,.125,8,300,0.0000008,-0.0000008]):
        floats(clock+0x40,time);write(clock+0x44,epoch);floats(clock+0x48,span);write(clock+0x4c,flags)
        floats(clock_delta,increment)
        supplied=read(clock+0x40,4)+read(clock_delta)
        saved=[machine.reg_read(reg) for reg in [UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]]
        machine.reg_write(UC_X86_REG_EDX,clock)
        run(0x6f054190,clock_delta)
        assert machine.reg_read(UC_X86_REG_EAX)==1 and machine.reg_read(UC_X86_REG_ESP)==stack+4
        assert saved==[machine.reg_read(reg) for reg in [UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]]
        assert read(clock+0x48,2)==supplied[2:4] and read(clock+0x20)==[1]
        actual=read(clock+0x40,3)+[int(read(clock+0x44)[0]!=epoch)]
        if engine:
            arg=(ctypes.c_uint32*5)(*supplied);result=(ctypes.c_uint32*4)()
            engine.pathing_clock_advance(arg,result)
            assert list(result)==actual,(supplied,list(result),actual)
            assert list(arg)==supplied
        clock_controls.append(dict(input=supplied,output=actual))
    if args.clock_trajectory_fixture:
        args.clock_trajectory_fixture.write_text(json.dumps(dict(binary_sha256=digest,
            origin=[float_bits(-2048),float_bits(512)],position=[float_bits(1.125),float_bits(1.75)],
            speed=float_bits(100),heading=float_bits(.125),advances=clock_advances,steps=clock_trajectory,frames=clock_frames,controls=clock_controls,
            scope='Complete primary producer04c1a0 with empty original timer/request heaps; supplied5ms source and controlled mover commit every6 advances before the next advance. Retained spatial records, old-velocity integration and scalar world query. Live source/owner order separately observed; complete original owner route is excluded.'),separators=(',',':'))+'\n')
    if args.native_pose_fixture:
        args.native_pose_fixture.write_text(json.dumps(dict(binary_sha256=digest,sequences=native_pose_sequences,
            scope='Retained native pose/spatial records with supplied constant elapsed; zero-elapsed velocity then integration and scalar world inverse. Original owner/public-clock producer excluded.'),separators=(',',':'))+'\n')
    report=dict(binary_sha256=digest,callback_mutation_scope="Controlled external requests at original slot54 entry; complete gameplay callback graph and handle reclaim/reuse excluded",group_callback_mutation_cases=len(callback_mutations),group_callback_mutations=normalized_mutations,group_callback_mutation_digest=mutation_digest,move_owner_active_separation_cases=len(active_pair_cases),move_owner_active_separation_trajectories=active_pair_cases,move_owner_separation_cases=owner_separation_cases,move_owner_arrival_cases=len(owner_arrival_cases),move_owner_trajectories=owner_arrival_cases,move_adaptive_arrival_cases=len(adaptive_arrival_cases),move_adaptive_trajectories=adaptive_arrival_cases,move_obstacle_arrival_cases=len(obstacle_arrival_cases),move_obstacle_exact_repeat=obstacle_arrival_cases[0]==obstacle_arrival_cases[1],move_obstacle_trajectories=obstacle_arrival_cases,
                move_all_elapsed_cases=len(all_elapsed_cases),move_all_elapsed_integration_ticks=sum(row['ticks'] for row in all_elapsed_cases),
                move_all_elapsed_task_reclamations=2*len(all_elapsed_cases),move_all_elapsed_group_path_releases=len(all_elapsed_cases),
                move_transform_max_error=max(row['matrix_max_error'] for row in all_elapsed_cases),
                unit_ui=dict(rawcode='hfoo',source='Units/UnitUI.slk in installed war3.mpq, War3x.mpq and War3Patch.mpq',
                    max_pitch_degrees=10,max_roll_degrees=10,elevation_radius=20,
                    conversion='Matches689130 x87(value*float32pi/180) thenfloat32store; full SLK loader not executed',
                    cache_setters=['352db0','352dd0','352cf0'],
                    terrain='Prebuilt16x16 mode0 cached height layer with32-unit centered samples; mode−1 support uses5x5 packed corners. Flat and slope1/8 plane inputs; native grid construction not executed'),move_slope_arrival_cases=len(slope_arrival_cases),move_slope_trajectories=slope_arrival_cases,move_stock_ui_arrival_cases=len(stock_ui_arrival_cases),move_stock_ui_trajectories=stock_ui_arrival_cases,crt=dict(path=str(args.binary.parent/'msvcr120.dll'),sha256=crt_digest,base=hex(crt_base),exports=resolved_crt_exports,
                    scope='Original shipped PE sections mapped; HIGHLOW relocations and six game import slots resolved. DLL startup, other imports and independent CRT trig bit reference not executed'),
                move_elapsed_arrival_cases=len(elapsed_arrival_cases),move_elapsed_trajectories=elapsed_arrival_cases,
                move_elapsed_integration_ticks=sum(row['ticks'] for row in elapsed_arrival_cases),move_elapsed_deferred_task_reclamations=2*len(elapsed_arrival_cases),move_elapsed_group_path_releases=len(elapsed_arrival_cases),scope=__doc__,passed=True,group_member_decision_cases=decision_cases,group_member_decision_hold_cases=decision_hold_cases,group_member_route_commit_cases=decision_route_cases,group_cached_tick_cases=tick_cases,group_membership_prepass_cases=prepass_cases,group_completion_cases=completion_cases,move_subscriber_dispatch_prefix_cases=subscriber_prefix_cases,move_arrival_dispatch_cases=arrival_dispatch_cases,move_active_arrival_task_pop_cases=active_arrival_dispatch_cases,move_arrival_next_task_rejection_cases=arrival_next_task_rejection_cases,move_arrival_next_task_acceptance_cases=arrival_next_task_acceptance_cases,move_arrival_fresh_tick_cases=arrival_fresh_tick_cases,unit_order_queue_pop_cases=queue_prefix_cases,deferred_wrapper_release_cases=queue_prefix_cases,owner_payload_release_cases=queue_prefix_cases//2,group_completion_threshold=completion_threshold,group_completion_sentinel=completion_sentinel,regroup_advance_cases=regroup_advance_cases,regroup_status_cases=regroup_cases,regroup_squared_thresholds=regroup_thresholds,formation_refresh_cases=formation_refresh_cases,formation_full_layout_cases=full_layout_cases,formation_full_layout_max_error=full_layout_error,formation_rank_gap=rank_gap,formation_depth_gap=depth_gap,formation_center_rotate_cases=center_rotate_cases,formation_center_rotate_max_error=center_rotate_error,formation_row_placement_cases=row_placement_cases,formation_row_dimension_cases=row_dimension_cases,formation_row_tables=row_tables,formation_row_gap=row_gap,formation_layout_bucket_cases=layout_bucket_cases,formation_layout_max_error=layout_error,formation_step_cases=formation_step_cases,formation_setter_cases=formation_setter_cases,group_formation_boundary_intervals=formation_boundary,group_formation_cases=formation_cases,group_formation_padding=formation_padding,group_registered_release_cases=registered_release_cases,group_stop_cases=group_cases,group_shared_lifecycle_cases=lifecycle_cases,group_shared_radius_sequences=radius_sequences,group_aux_ownership_cases=ownership_cases,group_aux_publication_sequences=publication_sequences,group_radius_cases=radius_cases,group_speed_cap_cases=cap_cases,group_speed_commit_cases=group_cases,velocity_commit_cases=len(velocity_cases),post_velocity_integration_cases=len(velocity_cases),velocity_max_error=max(row['error'] for row in velocity_cases),position_integration_cases=integration_cases*2,integration_sequences=integration_cases,time_deadzone=time_deadzone,time_sequences=time_sequences,time_boundary_cases=time_boundary_cases,speed_heading_cases=cases,speed_producer_cases=speed_producer_cases,heading_error_cases=angle_cases,max_heading_error=max_angle_error,heading_deadzone=deadzone,turn_producer_cases=turn_producer_cases,deadzone_boundary_cases=deadzone_cases,normalization_samples=normalization,
                boundary_policy='abs(delta)>=threshold stops translation; turning still applies',
                exclusions=['non-binary-fraction rounding outside the implemented C helper comparisons','group target-speed adjustment, adaptive intermediate-waypoint progression, crowded ticks and cant-path consumers','integration with general float rounding and mixed-object occupancy',
                            'full owner singleton executes scheduler and visual-facing updates with empty shared/separation lists; two controlled post-arrival repulsors also execute alternating separation; active singleton plus eligible repulsor also composes; crowded active groups and mixed profiles remain open',
                            'accepted next task uses recycled CPrCluster/member buffer; first-ever Storm allocation not executed',
                            'elapsed arrivals cover controlled zero UI limits, stock hfoo UI on flat terrain and one slope1/8 plane; one static wall detour also composed; bridge geometry, water, limit clamping, crowds and unreachable outcomes remain open'])
    report.update(primary_clock_advances=len(clock_advances),primary_clock_motion_commits=len(clock_trajectory),
                  engine_primary_clock_advances=len(clock_advances) if engine else 0,
                  primary_clock_controls=len(clock_controls),engine_primary_clock_controls=len(clock_controls) if engine else 0,
                  position_write_cases=len(position_cases),position_next_move_cases=len(position_cases),
                  engine_position_write_cases=len(position_cases) if engine else 0,
                  engine_position_next_move_cases=len(position_cases) if engine else 0)
    report.update(native_pose_sequences=len(native_pose_sequences),native_pose_commits=sum(len(s['steps']) for s in native_pose_sequences),
                  native_pose_world_differences=sum(r['direct_world']!=r['output'][2:] for s in native_pose_sequences for r in s['steps']),
                  engine_native_pose_commits=288 if engine else 0)
    assert len(formation_raw_cases) == 865 and mixed_formation_cases == 720
    report.update(exact_formation_cases=len(formation_raw_cases), mixed_formation_cases=mixed_formation_cases,
                  engine_formation_cases=len(formation_raw_cases) if engine else 0)
    if engine:
        report.update(engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest(),
                      engine_exact_decision_cases=cases, engine_exact_angle_cases=len(normalization),
                      engine_exact_velocity_cases=len(velocity_cases),engine_exact_integration_cases=engine_integrations,
                      engine_exact_world_velocity_cases=len(world_velocity_cases) if engine else 0,engine_exact_committed_facing_cases=len(velocity_cases) if engine else 0,
                      engine_exact_velocity_heading_cases=len(facing_cases) if engine else 0,
                      engine_exact_facing_angle_cases=len(facing_angles) if engine else 0,
                      engine_exact_heading_error_cases=angle_cases+heading_boundary_cases+deadzone_cases)
    if args.primary_route_fixture or args.primary_route_reference:
        assert len(primary_arrival_cases)==2
        primary_arrival_cases=json.loads(json.dumps(primary_arrival_cases))
        assert primary_arrival_cases[0]==primary_arrival_cases[1]
        payload=dict(version=1,binary_sha256=digest,source_entry='6f15aa80',primary_step_bits=float_bits(.005),
            phases_per_owner=6,cell_world=32,world_origin=[0,0],cases=primary_arrival_cases,
            scope='Complete controlled original owner/group/member; authenticated clock advances six 5 ms intervals per pass. A fresh callback at clock 0 is supplied. Public order-admission phase, stock profiles, shared groups and live scenes are excluded.')
        if args.primary_route_reference:
            frozen=json.loads(args.primary_route_reference.read_text())
            assert all(payload[key]==frozen[key] for key in ('version','binary_sha256','source_entry','primary_step_bits','phases_per_owner','cell_world','world_origin'))
            assert len(frozen['cases'])==1 and all(case==frozen['cases'][0] for case in primary_arrival_cases)
            report['primary_route_reference_sha256']=hashlib.sha256(args.primary_route_reference.read_bytes()).hexdigest()
        if args.primary_route_fixture:
            args.primary_route_fixture.parent.mkdir(parents=True,exist_ok=True)
            args.primary_route_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
        report['primary_route_cases']=len(primary_arrival_cases)
        report['primary_route_ticks']=sum(c['ticks'] for c in primary_arrival_cases)
    if args.route_trajectory_fixture or args.route_trajectory_reference:
        route_cases=[{**{key:row[key] for key in ('blocked','start','goal','route','ticks','initial_motion','radius_bits','radius_world')},
            'steps':[{key:step[key] for key in ('tick','start_bits','position_bits','velocity_bits','heading_bits','waypoint')} for step in row['steps']]} for row in obstacle_arrival_cases]
        # Compare the persisted representation: synthetic coordinates are tuples before JSON serialization.
        route_cases=json.loads(json.dumps(route_cases))
        payload=dict(version=1,binary_sha256=digest,source_entry='6f16c150',elapsed_bits=float_bits(1/32),world_origin=[0,0],cell_world=32,
            cases=route_cases,scope='Complete controlled original group/member route setup, fine search, progression, steering, commits and natural arrival. Supplied radius.25/mask02000000; stock hfoo movement profile and primary owner cadence excluded.')
        if args.route_trajectory_reference:
            frozen=json.loads(args.route_trajectory_reference.read_text())
            assert all(payload[key]==frozen[key] for key in ('version','binary_sha256','source_entry','elapsed_bits','world_origin','cell_world'))
            assert len(frozen['cases'])==1 and all(row==frozen['cases'][0] for row in route_cases)
            report['route_trajectory_exact_replays']=len(route_cases)
            report['route_trajectory_steps']=sum(row['ticks'] for row in route_cases)
            report['route_trajectory_reference_sha256']=hashlib.sha256(args.route_trajectory_reference.read_bytes()).hexdigest()
    if args.route_trajectory_fixture:
        args.route_trajectory_fixture.parent.mkdir(parents=True,exist_ok=True)
        args.route_trajectory_fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
