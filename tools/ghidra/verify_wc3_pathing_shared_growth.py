#!/usr/bin/env python3
"""Original shared-owner pool growth, publication and reuse; Storm storage only.

Uses the saved spatial harness's original owner/registry constructors. Radius
collection uses supplied valid group/member identity bindings as in the prior
128-case composed oracle. It is separate from public Captain journey evidence.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parent/'research'))
import sep03_map05_spatial_harness as H


def validate_report(report):
    expected=dict(passed=True,binary_sha256=H.SHA256,owners=129,block_size=64,
                  block_bytes=3076,growth_allocations=3,reuse_allocations=0,final_live=0)
    if any(report.get(k)!=v for k,v in expected.items()):raise ValueError('shared growth/reuse incomplete')
    phases=report['publication_phases']
    if len(phases)!=3 or [r['departed']for r in phases]!=[False,False,True]:
        raise ValueError('shared radius phases incomplete')
    for row in phases:
        expected_radius=row['radii'][0]if row['departed']else max(row['radii'])
        if row['references']!=2 or row['published']!=[0x3f000000,0x7f7fffff,expected_radius]:
            raise ValueError('shared publication differs')
    return report


def verify(binary):
    e=H.Emu(binary);e.call(0x6f003c40)
    w=H.World(e,8,8,factory=True);pool=w.owner+0x658
    assert e.r(pool,2)==[48,64]
    start=len(e.log);objects=[]
    for i in range(129):
        obj=e.call(0x6f155490,pool,0);assert e.esp_after==8
        assert e.r(obj+0x14,3)==[0xffffffff,0xffffffff,0]
        assert e.r(obj+0x20,3)==[0x7f7fffff,0x7f7fffff,0]
        objects.append(obj)
        assert e.r(pool+0x18)==i+1 and len(set(objects))==len(objects)
    allocations=e.log[start:]
    assert len(allocations)==3 and all(r['op']=='alloc'and r['size']==3076 for r in allocations)
    # Every old owner survives all three raw pool blocks.
    for obj in objects:assert e.r(obj)==0x6fa90c98 and e.r(obj+0x1c)==0
    shared=objects[64]
    groups=[e.fixture(0x100),e.fixture(0x100)]
    movers=[e.fixture(0x100),e.fixture(0x100)]
    members=[e.fixture(0x2c),e.fixture(0x2c)]
    slots=e.fixture(32);e.w(w.registry+0xc,slots);e.w(w.registry+0x1c,4)
    for i,(g,m,a)in enumerate(zip(groups,members,movers)):
        e.w(g+0x18,222);e.w(g+0x28,m);e.w(g+0x38,1)
        e.w(m,i,111);e.w(m+0x14,a);e.w(a+0x18,111)
        e.w(a+0x9c,i+2,222);e.w(slots+i*8,-2,a);e.w(slots+(i+2)*8,-2,g)
        e.call(0x6f16d890,g,shared)
    assert e.r(shared+0x1c)==2
    scalar=lambda f:struct.unpack('<I',struct.pack('<f',f))[0]
    phases=[]
    for radius,departed in [((.75,1.75),False),((.75,3.25),False),((.75,3.25),True)]:
        e.w(shared+0x24,scalar(.5));e.call(0x6f16c220,shared)
        assert e.r(shared+0x20,3)==[scalar(.5),0x7f7fffff,0]
        for i,g in enumerate(groups):
            e.w(movers[i]+0x90,scalar(radius[i]))
            e.w(movers[i]+0x9c,0xffffffff if i==1 and departed else i+2,222)
            e.call(0x6f16e1f0,g)
        expected=scalar(radius[0]if departed else max(radius))
        assert e.r(shared+0x28)==expected
        for g in groups:
            out=e.fixture(4);e.call(0x6f16c940,g,out);assert e.r(out)==expected
        phases.append(dict(radii=[scalar(r)for r in radius],departed=departed,published=e.r(shared+0x20,3),references=e.r(shared+0x1c)))
    for g in groups:e.call(0x6f16d890,g,0)
    assert e.r(shared+0x1c)==0
    # Zero-reference publication returns objects to the constructed-object free
    # list. It does not free their raw64-element blocks.
    for i,obj in enumerate(objects):
        e.call(0x6f16c220,obj);assert e.r(pool+0x18)==128-i
    assert e.r(pool+0x14)==objects[-1]-4
    before=len(e.log)
    reused=[e.call(0x6f155490,pool,0)for _ in objects]
    assert reused==list(reversed(objects)) and len(e.log)==before
    assert e.r(pool+0x18)==129
    for obj in reused:assert e.r(obj+0x1c,4)==[0,0x7f7fffff,0x7f7fffff,0]
    for obj in reused:e.call(0x6f16c220,obj)
    assert e.r(pool+0x18)==0
    return validate_report(dict(passed=True,binary_sha256=H.SHA256,owners=129,block_size=64,block_bytes=3076,
        growth_allocations=3,reuse_allocations=0,final_live=0,publication_phases=phases,
        scope='Original owner/registry/pool constructors and129 shared factories, three64-element Storm-backed blocks, original reference setters/publication/radius collection, zero-reference release and exact LIFO reuse. Supplied group/member registry bindings; public Captain trajectories remain separately verified.'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--fixture',type=Path);args=ap.parse_args()
    report=verify(args.binary)
    if args.fixture and report!=json.loads(args.fixture.read_text()):raise ValueError('frozen shared growth differs')
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
