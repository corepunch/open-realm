#!/usr/bin/env python3
"""Original point factories with owned children, borrowed relations and pool reuse.

Class/identity bindings and the point's extra owned reference are supplied.
Only Storm memory imports use host storage; all game calls/vtables are original.
"""
import itertools
from foot03_spatial_harness_copy import Emu

CLASSES=(
 ('COrderPoint',0x6fd70e14,0x6fb7885c,0x6f6807a0,0x6fb7886c,0x58,1,0x54,0x6f72642e,0x6f015630,0x6f01564d),
 ('CTaskPoint',0x6fd70f1c,0x6fb78eb0,0x6f680db0,0x6fb78ec0,0x50,64,0x4c,0x74736b2e,0x6f0158a0,0x6f0158bd),
)

def original(binary):
 from unicorn import UC_HOOK_CODE
 rows=[]
 for cls,nested,holes,extra in itertools.product(CLASSES,(False,True),(False,True),(False,True)):
  name,factory,vt,construct,payload_vt,size,block,owned,rawcode,init,end=cls
  e=Emu(binary);x=e.X
  registry=e.fixture(0x100);e.call(0x6f04a6b0,registry);e.w(0x6fd6860c,registry)
  owner=e.fixture(0x1000);e.call(0x6f157610,owner,7085);e.w(0x6fd53a48,owner);e.w(owner+0x254,0x6f04d9c0)
  host=e.fixture(0x100);e.w(0x6fd3c82c,host);e.w(host+0x30,-1)
  e.w(e.stack,e.stop);e.uc.reg_write(x.UC_X86_REG_ESP,e.stack);e.uc.emu_start(init,end,count=10000)
  assert e.uc.reg_read(x.UC_X86_REG_EIP)==end and e.r(factory)==vt
  bucket=e.fixture(16);entry=e.fixture(0x80);hash_input=e.fixture(4)
  e.w(hash_input,rawcode);hashed=e.call(0x6f198420,hash_input)
  e.w(host+0x28,bucket,0,0);e.w(bucket,0,0,entry);e.w(entry+4,hashed);e.w(entry+0x18,rawcode);e.w(entry+0x70,factory)
  pool=e.call(0x6f057470);count=11 if nested else 9;slots=e.fixture(8*count)
  e.w(registry+0xc,slots);e.w(registry+0x1c,count)
  destroyed=[];returned=[];reclaimed=[]
  def watch(uc,address,_size,_data):
   ptr=uc.reg_read(x.UC_X86_REG_ECX)
   if address==0x6f145c70:destroyed.append(ptr)
   elif address==0x6f0576b0:returned.append(ptr)
   else:reclaimed.append(ptr)
  for address in (0x6f145c70,0x6f0576b0,0x6f03e9c0):e.uc.hook_add(UC_HOOK_CODE,watch,begin=address,end=address)
  prior_wrappers=prior_payloads=prior_returns=None;phases=[]
  for cycle in range(2):
   start_alloc=len(e.log);wrappers=[];payloads=[]
   for i in range(count):
    w=e.call(0x6f057350,pool,0,0);p=e.call(construct,factory)
    e.call(0x6f057c30,w,p)
    assert e.r(p)==payload_vt and e.r(p+4)==1 and e.r(p+owned)==0
    assert e.r(w+0x74)==0 and all(e.r(w+o)==0 for o in (0x7c,0x8c,0x9c,0xac))
    e.w(w+0x14,i,100+i+cycle*count);e.w(p+0xc,i,100+i+cycle*count);e.w(slots+8*i,-2,w)
    wrappers.append(w);payloads.append(p)
   e.w(registry+0x40,-1);e.w(registry+0x48,count)
   if cycle:
    assert wrappers==list(reversed(prior_returns))
    # Payload reclaim order may differ from wrapper order with shared references.
    assert payloads==list(reversed(prior_payloads))
    assert len(e.log)==start_alloc
   factory_imports=len(e.log)-start_alloc
   root=wrappers[0];scratch=e.fixture(4)
   children=[5,6,7,8]
   child_slots=[]
   for i in children:
    if holes:
     e.w(scratch,0);assert e.call(0x6f146850,root+0x58,scratch,1)==1
    child_slots.append(e.r(root+0x74));e.w(scratch,wrappers[i]);assert e.call(0x6f146850,root+0x58,scratch,1)==1
   for category,(borrowed,child) in enumerate(zip(range(1,5),children)):
    # Two entries in each category: borrowed peer and owned child. Native
    # producer supplies exact link back-pointers and newest-first insertion.
    e.call(0x6f15e180,wrappers[borrowed],root,root+0x78+16*category)
    e.call(0x6f145dc0,root,child_slots[category],category)
    assert e.r(root+0x7c+16*category)==wrappers[child]+0x24
    assert e.r(wrappers[borrowed]+0x30)==root and e.r(wrappers[child]+0x30)==root
   if nested:
    for i in (9,10):
     e.w(scratch,wrappers[i]);assert e.call(0x6f146850,wrappers[8]+0x58,scratch,1)==1
     e.call(0x6f145dc0,wrappers[8],i-9,i-9)
   if extra:
    e.w(payloads[0]+owned,payloads[5]);e.w(payloads[5]+4,2)
   preparation_imports=len(e.log)-start_alloc
   borrowed_payloads=[bytes(e.uc.mem_read(payloads[i],size))for i in range(1,5)]
   before_destroy=len(destroyed);before_return=len(returned);before_reclaim=len(reclaimed)
   e.call(0x6f0557b0,payloads[0]);e.call(0x6f0557b0,payloads[0]);clock=owner+0x14
   assert e.r(clock+0x3c)==1
   e.w(clock+0x40,0x3f800000+cycle*0x1000000);e.call(0x6f052380,clock)
   def ordinal(items):return [wrappers.index(p)for p in items]
   expected_destroy=[0,8]+([10,9]if nested else [])+[7,6,5]
   expected_return=([10,9]if nested else [])+[8,7,6,5,0]
   assert ordinal(destroyed[before_destroy:])==expected_destroy,(name,nested,holes,extra,cycle,ordinal(destroyed[before_destroy:]),expected_destroy)
   assert ordinal(returned[before_return:])==expected_return
   assert len(reclaimed)-before_reclaim==len(expected_destroy)
   assert e.r(pool+0x18)==4 and e.r(factory+0xc)==4 and e.r(registry+0x48)==4
   for i in range(count):
    assert e.r(wrappers[i]+0x74)==0 and all(e.r(wrappers[i]+o)==0 for o in (0x7c,0x8c,0x9c,0xac))
    assert [e.r(wrappers[i]+o)for o in (0x24,0x28,0x30)]==[0,0,0],(i,e.r(wrappers[i]+0x24,4))
    if 1<=i<=4:
     assert e.r(slots+8*i,2)==[0xfffffffe,wrappers[i]]
     assert bytes(e.uc.mem_read(payloads[i],size))==borrowed_payloads[i-1]
    else:
     assert e.r(slots+8*i+4)==0 and e.r(wrappers[i]+0x54)==0
   # Borrowed peers survive root release; release them individually afterward.
   for i in range(1,5):e.call(0x6f0557b0,payloads[i])
   e.w(clock+0x40,0x40000000+cycle*0x1000000);e.call(0x6f052380,clock)
   assert e.r(clock+0x3c)==0 and e.r(pool+0x18)==0 and e.r(factory+0xc)==0 and e.r(registry+0x48)==0
   assert len(returned)-before_return==count and len(set(returned[before_return:]))==count
   snapshot=bytes(e.uc.mem_read(slots,8*count));e.call(0x6f052380,clock);assert bytes(e.uc.mem_read(slots,8*count))==snapshot
   assert e.r(0)==0
   phases.append(dict(destroy_order=ordinal(destroyed[before_destroy:before_destroy+len(expected_destroy)]),
    child_return_order=ordinal(returned[before_return:before_return+len(expected_return)]),
    complete_return_order=ordinal(returned[before_return:]),borrowed_survivors=4,final_live=0,
    zero_pending=True,seh_balanced=True,factory_imports=factory_imports,preparation_imports=preparation_imports,
    lifetime_imports=len(e.log)-start_alloc))
   prior_wrappers=wrappers;prior_payloads=list(reclaimed[before_reclaim:]);prior_returns=list(returned[before_return:])
  rows.append(dict(name=name,nested=nested,null_slots=holes,extra_child_reference=extra,objects=count,phases=phases))
 return rows
