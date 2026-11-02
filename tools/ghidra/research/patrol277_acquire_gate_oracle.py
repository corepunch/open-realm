#!/usr/bin/env python3
"""Original697ea0 dispatch gate with synthetic unit/Attack and labelled downstream recorder.
No retail bytes are distributed. Does not claim downstream property-store or JASS parser behavior.
"""
import argparse,hashlib,json,pathlib
from foot03_spatial_harness_copy import Emu,SHA256
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_patrol277_acquire_gate.json'
CASES=[('negative',0xbf800000),('negative-zero',0x80000000),('zero',0),('below50',0x4247ffff),('equal50',0x42480000),('above50',0x42480001),('137.25',0x43094000),('positive-infinity',0x7f800000),('negative-infinity',0xff800000),('quiet-NaN',0x7fc00001),('signaling-NaN',0x7f800001),('negative-quiet-NaN',0xffc00001)]
def run(binary):
 from unicorn import UC_HOOK_CODE
 e=Emu(binary);X=e.X;e.w(0x6fd3c7ac,0xdeadbeef);e.call(0x6f001cd0);assert e.r(0x6fd3c7ac)==0x42480000
 unit=e.fixture(0x300);attack=e.fixture(0x300);arg=e.fixture(16);calls=[]
 def record(uc,address,size,data):
  sp=uc.reg_read(X.UC_X86_REG_ESP);p=e.r(sp+4);calls.append({'attack_matches':uc.reg_read(X.UC_X86_REG_ECX)==attack,'argument_matches':p==arg,'raw':e.r(p)})
  uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,e.r(sp))
 e.uc.hook_add(UC_HOOK_CODE,record,begin=0x6f49c3d0,end=0x6f49c3d0)
 rows=[]
 for attached in (False,True):
  for label,word in CASES:
   e.w(unit+0x1e8,attack if attached else 0);e.w(arg,word);calls.clear();e.uc.reg_write(X.UC_X86_REG_MXCSR,0x1f80)
   e.call(0x6f697ea0,unit,arg);assert e.esp_after==8 and e.r(arg)==word
   nan=(word&0x7f800000)==0x7f800000 and bool(word&0x7fffff)
   expected=attached and (nan or (word>>31==0 and word>0x42480000))
   assert bool(calls)==expected and len(calls)<=1
   assert not calls or calls==[{'attack_matches':True,'argument_matches':True,'raw':word}]
   rows.append({'case':label,'input_word':word,'attack_attached':attached,'dispatched':bool(calls),'calls':list(calls),'input_unchanged':True,'stack_delta':e.esp_after,'mxcsr_after':e.uc.reg_read(X.UC_X86_REG_MXCSR)})
 return {'schema':1,'original_sha256':SHA256,'initializer':{'entry':0x6f001cd0,'output_address':0x6fd3c7ac,'poison_before':0xdeadbeef,'actual_after':e.r(0x6fd3c7ac)},'rows':rows,'scope':['Unmodified original001cd0 integer initializer and697ea0 body executed in Unicorn.','Syntheticunit+Attack pointers;49c3d0 replaced by argument recorder returning RET4.','Only dispatch/no-dispatch and untouched argument/stack are proven. Downstream property behavior, unmasked floating exceptions and public NaN parser reachability are excluded.','COMISS unordered sets CF and dispatches NaN under maskedMXCSR; ordinary <=50 skips.']}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True);p.add_argument('--check',action='store_true');p.add_argument('--report',type=pathlib.Path);a=p.parse_args();d=run(a.binary)
 if a.check:
  assert d==json.loads(FIXTURE.read_text()),'original gate fixture changed'
  spec=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
  for n,h in spec['repository_files'].items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
 if a.report:a.report.write_text(json.dumps(d,indent=2)+'\n')
 print(json.dumps({'status':'PASS','cases':len(d['rows']),'initializer_word':d['initializer']['actual_after']}))
if __name__=='__main__':main()
