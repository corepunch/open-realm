#!/usr/bin/env python3
"""Complete original completion bridge, supplied guard/identity states.

No substituted game instructions or callbacks. Invalid guards are labelled
supplied states, not claims that public gameplay can fabricate stale handles.
"""
import itertools
from foot03_spatial_harness_copy import Emu

STATES=('valid','no_host','disabled_host','stale','free','outside','wrong_type',
        'pending_release','no_payload','unknown_tag','invalid_identity')
TAGS=(0x63702661,0x6370266f,0x63702670)

def original(binary):
 from unicorn import UC_HOOK_CODE
 e=Emu(binary);x=e.X
 host=e.fixture(0x100);registry=e.fixture(0x100);positive=e.fixture(0x40);negative=e.fixture(0x40)
 wrapper=e.fixture(0x100);unit=e.fixture(0x400);table=e.fixture(0x40);buckets=e.fixture(0x40);packet=e.fixture(0x40)
 events=[];pins=[]
 def observe(uc,address,size,data):
  if address==0x6f071e00:pins.append(e.r(unit+4));return
  sp=uc.reg_read(x.UC_X86_REG_ESP);code,record=e.r(sp+4,2)
  events.append([uc.reg_read(x.UC_X86_REG_ECX),code,e.r(record+8),e.r(record+0x10)])
 for a in (0x6f071dc0,0x6f071e00):e.uc.hook_add(UC_HOOK_CODE,observe,begin=a,end=a)
 rows=[]
 for domain,refs,tag,state in itertools.product((0,1),(1,3),TAGS,STATES):
  for p,n in ((host,0x100),(registry,0x100),(positive,0x40),(negative,0x40),
              (wrapper,0x100),(unit,0x400),(table,0x40),(buckets,0x40),(packet,0x40)):
   e.uc.mem_write(p,bytes(n))
  identity=(domain<<31)|1;slots=negative if domain else positive
  e.w(0x6fd3c82c,0 if state=='no_host'else host);e.w(host+8,state!='disabled_host')
  e.w(0x6fd68610,registry);e.w(registry+0xc,positive);e.w(registry+0x2c,negative)
  e.w(registry+0x1c,2);e.w(registry+0x3c,2);e.w(slots+8,-2,wrapper)
  e.w(wrapper,0x6fa8099c);e.w(wrapper+0xc,0x2b61676c,0x2b616761,identity,105)
  e.w(wrapper+0x54,0 if state=='no_payload'else unit);e.w(wrapper+0x20,state=='pending_release')
  e.w(unit,0x6fb77eb0,refs,table);e.w(unit+0xc,identity,105)
  e.w(table,0x10100,buckets)
  if state=='stale':e.w(unit+0x10,104)
  if state=='free':e.w(slots+8,-1,0)
  if state=='outside':e.w(registry+(0x3c if domain else 0x1c),1)
  if state=='wrong_type':e.w(wrapper+0xc,0x2b616761)
  if state=='invalid_identity':e.w(unit+0xc,0xffffffff,0xffffffff)
  actual_tag=0 if state=='unknown_tag'else tag
  e.w(packet,0x5e70726f,0x60706375,actual_tag,0,0,0,0,0,0,-1,-1)
  resolved=e.call(0x6f054530,e.r(unit+0xc),edx=e.r(unit+0x10))
  assert resolved==(0 if state in ('stale','free','outside','invalid_identity')else wrapper)
  objects=[(p,bytes(e.uc.mem_read(p,n)))for p,n in ((host,0x100),(registry,0x100),(positive,0x40),
            (negative,0x40),(wrapper,0x100),(unit,0x400),(table,0x40),(buckets,0x40),(packet,0x40))]
  events.clear();pins.clear();e.w(0,0)
  e.call(0x6f057590,wrapper,packet);assert e.esp_after==8
  accepted=state=='valid';event=0x40190065 if tag==TAGS[0]else 0x40190066
  assert events==([[unit,event,event,unit]]if accepted else []),(domain,refs,tag,state,events)
  assert pins==([refs+1]if accepted else []),(state,pins)
  assert e.r(unit+4)==refs and e.r(0)==0
  for p,before in objects:assert bytes(e.uc.mem_read(p,len(before)))==before,(state,hex(p))
  rows.append(dict(domain='negative'if domain else'positive',references=refs,tag=tag,state=state,
                   dispatched=accepted,event=event if accepted else 0,pin_references=list(pins),balanced=True))
 assert not e.log
 return rows
