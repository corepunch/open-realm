"""Preallocate storage, then execute the retail no-file map and mover producers.

This supplies allocator backing and existing unit/ability state, never replacement
code. The caller owns image loading, register/stack validation and owner ticks.
"""
import struct


def create_owned_path(machine, context, run):
    """Supply recycled backing, then allocate/register through original14ec50."""
    owner,mover,path,inputs=(context[k] for k in ('owner','mover','path','inputs'))
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
    pool=owner+0x958
    before=read(pool+0x14,3)
    run(0x6f1657c0,path)
    write(path-4,before[0]);write(pool+0x14,path-4)
    run(0x6f14ec50,inputs,1,edx=0)
    assert read(inputs)==[path]
    assert read(pool+0x14,3)==[before[0],before[1]+1,before[2]+1]
    assert read(path+0x14,2)!=[0xffffffff]*2
    # Existing Unit/Move binding is supplied, as before; full callers remain BASE-03.1.
    write(mover+0xa8,path)


def construct(machine, context, run):
    owner, terrain, registry, slots, inputs = (context[k] for k in
        ('owner','terrain','registry','slots','inputs'))
    area = 0x10300000
    machine.mem_write(area, bytes(0x400000))
    def write(address, *words):
        machine.mem_write(address,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
    def read(address, count=1):
        return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def floats(address, *values):
        machine.mem_write(address,struct.pack('<%df'%len(values),*values))
    # Only the existing unit and its attached ability precede native map creation.
    assert read(slots+127*8,2)==[0xffffffff,0]
    assert read(registry+0x48)==[2]
    assert read(owner+0x234,8)==[0]*8
    # Original050a70 assigns04a6b0's return to6860c; that constructor also
    # publishes the same object at68610. The supplied registry backing must
    # reproduce both aliases. Spatial retirement14dae0 uses6860c, while
    # canonical resolution/unregistration uses68610. A zero alias can hide
    # stale spatial slots behind cleared object generations (GROUP-04.3).
    write(0x6fd6860c,registry)
    assert read(0x6fd6860c)==read(0x6fd68610)
    maps=[area+0x4004+n*0x200 for n in range(6)]
    storage=[area+0x10000+n*0x40000 for n in range(6)]
    dirty=[area+0x1a0000+n*0x1000 for n in range(2)]
    searches=[area+0x6004,area+0x6404]
    for n,(obj,data) in enumerate(zip(maps,storage)):
        if n<2:run(0x6f14c280,obj)
        else:write(obj,0x6fa90bd8)
        write(obj-4,maps[n+1]-4 if n in (0,2,3,4) else 0)
        size=0x8000*(4 if n<2 else 8)
        write(obj+0x20,data,size,data,size)
        write(obj+0x34,0x8000,0)
        if n<2:
            links=area+0x1b0000+n*0x10000
            write(obj+0x70,links,1024*8,links,1024*8)
            write(obj+0x84,1024,0)
            write(obj+0x90,dirty[n],4096,dirty[n],4096)
            write(obj+0xa4,1024,0)
    write(owner+0x598+0x14,maps[0]-4)
    write(owner+0x5b8+0x14,maps[2]-4)
    for obj,pool,vt,offset in zip(searches,[0x918,0x938],[0x6fa908ec,0x6fa90c40],[0x44,0x70]):
        write(obj,vt)
        write(owner+pool+0x14,obj-4)
        data=area+0x7000+offset*16
        write(obj+offset+4,data,12,data,12)
        write(obj+offset+0x18,1,1)
    index=area+0x8000
    write(searches[1]+0x34,index,256*12,index,256*12)
    write(searches[1]+0x48,256,0)
    clock=owner+0x164
    write(clock+0x10,area+0x9000)
    write(clock+0x1c,16,1)
    write(clock+0x38,area+0xa000)
    write(area+0xa000,area+0xa080)
    write(area+0xa080,0)
    for address,value in [(0x6fd3c740,-1),(0x6fd3c744,0),(0x6fd3c748,1),
            (0x6fd53a50,8),(0x6fd53a54,2),(0x6fd53a58,4),(0x6fd53a5c,8),(0x6fd53a60,16)]:
        floats(address,value)
    run(0x6f017dc0,0)
    bounds=area+0xb000
    run(0x6f78b0a0,bounds,edx=0)
    assert read(bounds,4)==[0,0,0x44000000,0x44000000]
    # Actual78c090 caller passes the initialized32-unit terrain cell and .03
    # simulation interval, not two unit scales. Loader derives the speed ceiling.
    run(0x6f017cd0,0)
    run(0x6f001e50,0)
    assert read(0x6fd723e8)==[0x42000000]
    assert read(0x6fd3c834)==[0x3cf5c290]  # Original software decimal parser, not host .03 rounding.
    run(0x6f04c860,bounds,0x6fd723e8,0x6fd3c834,edx=0)
    assert read(owner+0x234,6)==maps
    assert read(owner+0x24c,2)==searches
    dimensions=[(5,5),(16,16),(17,17),(8,8),(4,4),(2,2)]
    for obj,(w,h),step in zip(maps,dimensions,[8,1,2,4,8,16]):
        assert read(obj+0x3c,2)==[w,h]
        assert read(obj+0x54,4)==[0,0,h,w]
        assert struct.unpack('<2f',machine.mem_read(obj+0x64,8))==(step,1/step)
    assert read(registry+0x48)==[10]
    assert read(storage[1],256)==[0xffffff]*256
    # Construct recycled entries before exposing them to the original factories.
    mover=area+4
    run(0x6f14fa30,mover)
    write(owner+0x7d8+0x14,mover-4)
    regions=[area+0x1004,area+0x1104]
    for n,obj in enumerate(regions):
        run(0x6f14c1d0,obj)
        write(obj-4,regions[1]-4 if n==0 else 0)
    write(owner+0x5d8+0x14,regions[0]-4)
    run(0x6f14ee90,inputs,1,edx=0)
    assert read(inputs)==[mover]
    assert read(mover)==[0x6fa9129c]
    assert read(mover+0x94,2)==regions
    assert read(storage[1],256)==[0xffffff]*256
    assert read(regions[0]+0x2c)==[maps[0]]
    assert read(regions[1]+0x2c)==[maps[1]]
    assert read(owner+0x7d8+0x14,3)==[0,1,1]
    assert read(owner+0x5d8+0x14,3)==[0,2,2]
    mover_identity=read(mover+0x14,2)
    assert mover_identity==[8,108]
    write(context['unit']+0x16c,*mover_identity)
    # Producer inputs mirror the unit's attachment; parameter values are supplied
    # separately from the resulting mover/spatial state.
    write(inputs,0,0,0,context['unit_wrapper'])
    run(0x6f15fe30,mover,inputs)
    path=area+0x2004
    create_owned_path(machine,dict(owner=owner,mover=mover,path=path,inputs=inputs),run)
    return dict(mover=mover,current_path=path,maps=maps[:2],fine=searches[0],acc=searches[1],
        objects=regions,mover_identity=mover_identity,dimensions=dimensions,
        map_identities=[read(obj+0x14,2) for obj in maps],
        base_live_count=read(registry+0x48)[0],maintenance_requests=read(clock+0x20)[0]-1)
