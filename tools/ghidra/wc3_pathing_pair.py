"""Controlled second existing Unit/Move and original mover/shared-request producers.

Existing unit/class/subscriber backing has the same scope as the singleton
baseline. Mover/spatial activation, request membership and group attachment use
original instructions; no member row, route or runtime callback is replaced.
"""
import struct
import json
from pathlib import Path
from wc3_pathing_scenario import GAME_SHA,CRT_SHA,case_output,canonical_digest,first_difference

DEFAULT_FIXTURE=Path(__file__).parent/"fixtures/retail-shared-pair-1.27.json"


def provision(machine, context, run):
    owner,registry,slots,inputs=(context[k] for k in ('owner','registry','slots','inputs'))
    first_unit,first_ability=context['unit'],context['ability']
    spec=context['spec']
    unit,ability,wrapper,ability_wrapper,table,buckets=[0x10129000+n for n in (0,0x800,0x1000,0x1080,0x1100,0x1140)]
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    machine.mem_write(unit,bytes(machine.mem_read(first_unit,0x800)))
    machine.mem_write(ability,bytes(machine.mem_read(first_ability,0x400)))
    machine.mem_write(wrapper,bytes(machine.mem_read(context['unit_wrapper'],0x80)))
    machine.mem_write(ability_wrapper,bytes(machine.mem_read(context['ability_wrapper'],0x80)))
    write(slots+127*8,-2,wrapper)
    write(slots+125*8,-2,ability_wrapper)
    write(registry+0x48,read(registry+0x48)[0]+2)
    write(wrapper+0x14,127,904);write(wrapper+0x54,unit)
    write(ability_wrapper+0x14,125,905);write(ability_wrapper+0x54,ability)
    write(unit+4,1,table,127,904)
    write(unit+0x16c,-1,-1)
    write(unit+0x1dc,125,905)
    write(unit+0x284,*spec['second_position_bits'],0,0)  # World128,160; grid4,5.
    write(ability+4,1);write(ability+0xc,125,905);write(ability+0x30,unit)
    write(table,64<<8,buckets)
    for event in (0xd0148,0xd014c,0xd0162):run(0x6f0725b0,unit,event,event,unit)
    run(0x6f5fe6a0,ability,1)
    run(0x6f0725b0,unit,0xd0166,0xd0166,ability)
    assert read(unit+4)==[4]
    area=0x10550000
    mover=area+4;objects=[area+0x1004,area+0x1104];path=area+0x2004
    run(0x6f14fa30,mover)
    write(owner+0x7d8+0x14,mover-4)
    for index,obj in enumerate(objects):
        run(0x6f14c1d0,obj);write(obj-4,objects[1]-4 if index==0 else 0)
    write(owner+0x5d8+0x14,objects[0]-4)
    run(0x6f14ee90,inputs,1,edx=0)
    assert read(inputs)==[mover]
    write(unit+0x16c,*read(mover+0x14,2))
    write(inputs,0,0,0,wrapper);run(0x6f15fe30,mover,inputs)
    from wc3_pathing_baseline import create_owned_path
    create_owned_path(machine,dict(owner=owner,mover=mover,path=path,inputs=inputs),run)
    for offset,data in [(0x38,0x102a0000),(0x58,0x102a2000)]:
        write(path+offset,data,1024*8,data,1024*8)
        write(path+offset+0x14,1024,0)
    write(inputs,spec['second_radius_grid_bits']);run(0x6f15fef0,mover,inputs)
    write(inputs,spec['second_speed_world_bits']);run(0x6f05c5c0,unit+0x164,inputs)
    from verify_wc3_pathing_numeric import multiply
    write(inputs,*(multiply(w,0x3d000000) for w in spec['second_position_bits']));run(0x6f05c820,mover,inputs,0);run(0x6f170cf0,mover)
    write(inputs,spec['second_turn_bits']);run(0x6f05c8c0,unit+0x164,inputs)
    write(inputs,spec['second_window_bits']);run(0x6f05c890,unit+0x164,inputs)
    groups=[0x1012b004,0x1012c004];paths=[0x10553004,0x10554004];members=[0x10555000,0x10556000]
    for n,(group,route,rows) in enumerate(zip(groups,paths,members)):
        write(group,0x6fa90d64);write(group+0x14,-1,-1)
        write(group+0x1c,0x6fa90d5c,rows,12*0x2c,rows,12*0x2c,0,12,0)
        write(group+0x40,-1,-1)
        run(0x6f1657c0,route)
        for offset,data in [(0x38,0x102b0000+n*0x4000),(0x58,0x102b2000+n*0x4000)]:
            write(route+offset,data,1024*8,data,1024*8);write(route+offset+0x14,1024,0)
        write(group-4,groups[n+1]-4 if n==0 else 0)
        write(route-4,paths[n+1]-4 if n==0 else 0)
    write(context['group']-4,groups[0]-4)
    write(context['path']-4,paths[0]-4)
    return dict(unit=unit,ability=ability,mover=mover,path=path,objects=objects,
                groups=groups,paths=paths,members=members,base_live_count=read(registry+0x48)[0])


def join(machine, context, run):
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    inputs=context['inputs']
    run(0x6f058430,inputs,1,edx=0)
    request=read(inputs)[0]
    run(0x6f16db30,request,context['target'],0)
    for mover in context['movers']:run(0x6f169620,request,mover,1)
    run(0x6f16dc90,request,context['policy'])
    run(0x6f16bcf0,request)
    groups=[read(mover+0x9c,2) for mover in context['movers']]
    assert groups[0]==groups[1],('shared request did not join',groups)
    slot=read(context['slots']+groups[0][0]*8,2)
    assert slot[0]==0xfffffffe
    group=slot[1]
    assert read(group+0x38)==[2]
    return dict(group=group,path=read(group+0x3c)[0],request_identity=read(request+0x14,2))


def ground_profile(machine, context, run):
    """Observed hfoo profile cache, original CUnit collision/mask getters and bridge publication."""
    from unicorn.x86_const import UC_X86_REG_EAX
    profile=context['profile']
    spec=context['spec'].get('ground_profile')
    if spec is None:return
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*words))
    # Ground category/mask are observed live profile outputs; custom fixture radius stays8.
    write(profile+0x1ac,spec['category'],spec['query_mask'])
    write(profile+0x19c,spec['radius_world_bits'])
    for unit,mover in context['members']:
        # Execute the complete mask-producing prefix components of6945a0. Its
        # subsequent674310 ability-notification traversal needs the full class
        # descriptor graph (BASE-03.1), absent from this existing-unit fixture.
        run(0x6f678b50,unit);query=machine.reg_read(UC_X86_REG_EAX)
        run(0x6f678b60,unit);category=machine.reg_read(UC_X86_REG_EAX)
        assert query==spec['query_mask'] and category==spec['category']
        run(0x6f05c7e0,unit+0x164,category,query)
        path=read(mover+0xa8)[0];region=read(mover+0x98)[0]
        assert read(path+0x9c)==[(spec['query_mask'] & 0xffffff)|(spec['query_mask']<<24)]
        assert read(region+0x34)[0]&0xffffff==spec['category']
        assert read(unit+4)[0]==4


def terrain_edits(machine, context, run):
    """Original world terrain producer and explicit hierarchy refresh, preserving occupancy."""
    from verify_wc3_pathing_numeric import multiply
    spec,inputs,maps=(context[k] for k in ('spec','inputs','maps'))
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*words))
    edits=spec.get('terrain_edits',[])
    if not edits:return
    width,height=read(maps[1]+0x3c,2)
    cells=read(maps[1]+0x28)[0]
    before=read(cells,width*height)
    expected=list(before)
    for x,y,mask,blocked in edits:
        write(inputs,x,y)
        run(0x6f04d870,inputs,mask,blocked,edx=inputs+4)
        # Integer bit-to-float conversion is only for fixture coordinate indexing;
        # the original producer is authoritative for the edit.
        ix=int(struct.unpack('<f',struct.pack('<I',multiply(x,0x3d000000)))[0]//1)
        iy=int(struct.unpack('<f',struct.pack('<I',multiply(y,0x3d000000)))[0]//1)
        assert 0<=ix<width and 0<=iy<height
        old=expected[iy*width+ix]
        expected[iy*width+ix]=old|(mask<<24) if blocked else old&~(mask<<24)
        assert read(cells,width*height)==expected
    run(0x6f04e0b0,0)
    assert read(cells,width*height)==expected


def output(case):
    result=case_output(case)
    for field in ('shared_request','shared_pair_completed','second_dispatch','second_admissions','second_arrivals','auxiliary_dispatch'):
        result[field]=case[field]
    return json.loads(json.dumps(result))


def load_fixture(path=DEFAULT_FIXTURE):
    data=json.loads(Path(path).read_text())
    if data['version']!=1:raise ValueError('unsupported shared-pair fixture version')
    if data['build']!={'game_sha256':GAME_SHA,'crt_sha256':CRT_SHA}:raise ValueError('shared-pair build hashes differ')
    if data['inputs_sha256']!=canonical_digest(data['inputs']):raise ValueError('shared-pair inputs hash differs')
    if data['output_sha256']!=canonical_digest(data['output']):raise ValueError('shared-pair output hash differs')
    return data


def verify(case, fixture):
    actual=output(case)
    error=first_difference(actual,fixture['output'])
    if error:raise ValueError('shared-pair '+error)
    return canonical_digest(actual)
