#!/usr/bin/env python3
"""Unmodified original speed setter and complete primary/presentation clock wrappers.

Fixture TLS registry/network state, empty timer heaps and pause gates are supplied.
Only external Windows TLS/GetLastError and inherited Storm/CRT imports are adapted.
Pause gates below are labelled forced inputs, not public pause producer claims.
"""
import argparse,hashlib,importlib.util,json,struct,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
spec=importlib.util.spec_from_file_location('requests275',HERE/'verify_ORDER-05.1_request_heap.py')
requests=importlib.util.module_from_spec(spec);spec.loader.exec_module(requests)
from foot03_rig import Rig
def execute(binary_path):
 binary=Path(binary_path).read_bytes()
 m=requests.o31.Machine(binary);tls=m.obj(0x100);registry=m.obj(0x20);entries=m.obj(0x20);net=m.obj(0x2300);world=m.obj(0x100);settings=m.obj(0x80)
 m.w(tls+0xd*4,registry);m.w(registry+0x10,entries);m.w(entries+8,net);m.w(0x6fd687a8,world);m.w(world+0x30,settings)
 stub=m.emit(b'\xc2\x04\x00');m.labels[stub]=lambda m:m.u.reg_write(m.R['EAX'],tls);m.w(0x6fa7c244,stub)
 stub=m.emit(b'\x31\xc0\xc3');m.w(0x6fa7c290,stub)
 speeds=[]
 for locked in(0,0x4000,0x2000):
  for input in [*range(512),0x80000000,0xffffffff]:
   m.w(settings+0x38,locked);m.u.mem_write(settings+0x47,b'\x01');m.w(net+0x22a8,1,requests.bits(.8))
   b=requests.o31.CodeBuilder(m);b.b+=b'\x68'+struct.pack('<I',input);b.call(0x6f213350);b.b+=b'\x83\xc4\x04\xc3';m.call(b.done(),0)
   got=m.call(0x6f2001d0,0);canonical=1 if locked&0x4000 else min(input&255,2)
   assert got==canonical and m.r(net+0x22a8)==canonical
   assert m.r(net+0x22ac)==requests.bits((canonical+3)/5)
   speeds.append([input,locked,got,m.r(net+0x22ac)])
 r=Rig(binary_path);e=r.e
 # Registered 300-second initializer precedes construction in retail startup.
 e.call(0x6f001c40);r.owner=e.fixture(0xb00);e.call(0x6f157610,r.owner,0x69707365);e.w(0x6fd53a48,r.owner)
 # Timer-clock header shape is shared; queues are deliberately empty here.
 timer=e.fixture(0x54);e.uc.mem_write(timer,bytes(e.uc.mem_read(r.owner+0x14,0x54)));e.w(r.game+0x40,timer,timer)
 e.call(0x6f001e70)
 spans=[e.r(r.owner+offset+0x48)for offset in(0x14,0x68)]
 assert spans==[requests.bits(300)]*2
 timeline=[];delta=e.fixture(4)
 # Both independent gates, then ordinary and independent paused request clocks.
 for step,(domain,increment,gate,pause)in enumerate([
  (0,.005,0,0),(0,.005,1,0),(1,.025,0,0),(1,.025,1,0),
  (0,.005,0,1),(1,.025,0,1),(0,.005,0,0),(1,.025,0,0),
  *[(0,.005,0,0)for _ in range(20)],*[(1,.025,0,0)for _ in range(4)]]):
  clock=r.owner+(0x68 if domain else 0x14);e.w(r.game+(0x3c if domain else 0x38),gate)
  previous=e.r(clock+0x4c);e.w(clock+0x4c,(previous&~1)|pause);e.w(delta,requests.bits(increment))
  before=e.r(clock+0x40);e.call(0x6f04c0d0 if domain else 0x6f04c1a0,delta);after=e.r(clock+0x40)
  timeline.append([domain,requests.bits(increment),gate,pause,before,after,e.r(clock+0x44),e.r(clock+0x48)])
  assert (before==after)==bool(gate or pause)
  e.w(r.game+(0x3c if domain else 0x38),0);e.w(clock+0x4c,previous)
 wrapper=e.fixture(0x54);selectors=[]
 for identity in(0,1,0x7fffffff,0x80000000,0xffffffff):
  e.w(wrapper+0x14,identity);selected=e.call(0x6f15c650,r.owner,wrapper)
  assert selected==r.owner+(0x68 if identity&0x80000000 else 0x14)
  selectors.append([identity,selected-r.owner,e.r(selected+0x48)])
 return dict(binary_sha256=requests.SHA,speeds=speeds,timeline=timeline,selectors=selectors,spans=spans,limits=__doc__)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh output required')
 binary=a.binary.read_bytes();assert hashlib.sha256(binary).hexdigest()==requests.SHA
 result=execute(a.binary);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:len(v)for k,v in result.items()if isinstance(v,list)}))
if __name__=='__main__':main()
