#!/usr/bin/env python3
"""Original Warcraft III 1.27 log/exp/Pow, independent scalar models and explicit nontermination controls."""
import argparse, ctypes, json, struct, hashlib, random
from pathlib import Path
from verify_wc3_pathing_numeric import add, subtract, multiply, divide, reciprocal, integer_word, integer_float, MASK, SIGN, bits, initialize_runtime_scalars
from generate_wc3_math_tables import reciprocal_table
recips=reciprocal_table()
value=lambda w:struct.unpack('<f',struct.pack('<I',w&MASK))[0]
C=[0x3f612ad1,0x3fa8e5a3,0x3f28e5a3,0x3fdedc67,0xbe8df4e0,0xbf0df4e0,0xbe88d424,0xbf19bf59,0x3fa45af2,0x3effffbd,0x3e2ab479,0x3d296ec9,0x3c1a534c]

def trunc_word(w):
 exponent=((w>>23)&255)-127
 return 0 if exponent<0 else w if exponent>=23 else w & ((MASK << (23-exponent)) & MASK)

def ipow(base,n):
 if n&SIGN:raise ValueError('negative signed integer exponent does not terminate')
 result=bits(1)
 while n:
  if n&1:result=multiply(result,base)
  base=multiply(base,base);n>>=1
 return result

def corelog(w):
 t=divide(add(w,bits(-1)),add(w,bits(1)),recips)
 z=multiply(t,t)
 numerator=multiply(t,add(bits(1),multiply(C[6],z)))
 if numerator&0x7f800000:numerator=(numerator+0x800000)&MASK
 denominator=add(bits(1),multiply(C[7],z))
 return divide(numerator,denominator,recips)

def reducedlog(w):
 if value(w)>=value(C[0]):return corelog(w)
 i,j=(3,5) if not value(w)>=value(C[2]) else (1,4)
 return add(corelog(multiply(w,C[i])),C[j])

def log(w):
 mantissa=(w&0x7fffff)|bits(1)
 fraction=multiply(reducedlog(mantissa),0x3fb8aa3b)
 return multiply(add(integer_float((((w>>23)&255)-127)&MASK),fraction),0x3f317218)

def exp_positive(w):
 scaled=(w+(0x1000000 if w&0x7f800000 else 0))&MASK
 whole=trunc_word(scaled)
 fraction=subtract(scaled,whole)
 # Original raw sign-overflow guard around the divide-by-four exponent shift.
 raw=subtract(scaled,whole)
 fraction=0 if (((raw-0x1800000)&MASK)^raw)&SIGN else (raw-0x1000000)&MASK
 p=C[12]
 for term in [C[11],C[10],C[9],bits(1),bits(1)]:p=add(multiply(p,fraction),term)
 return multiply(p,ipow(C[8],integer_word(whole)))

def exp(w):
 negative=bool(w&SIGN and (w&~SIGN))
 out=exp_positive(w&~SIGN if negative else w)
 return reciprocal(out,recips) if negative else out

def power(a,b):
 if value(trunc_word(b))==value(b) and not (b&SIGN and b&~SIGN):return ipow(a,integer_word(b))
 if not a&0x7f800000:return 0
 return exp(multiply(b,log(a)))

def public(a,b):
 az=abs(value(subtract(a,0)))<value(0x3a83126f)
 bz=abs(value(subtract(b,0)))<value(0x3a83126f)
 if az and value(b)<0:return 0
 if not az and bz:return bits(1)
 return power(a,b)


def main():
 from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
 from unicorn.x86_const import (UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument("--binary",required=True,type=Path)
 parser.add_argument("--report",required=True,type=Path)
 parser.add_argument("--engine-library",type=Path)
 args=parser.parse_args()
 binary=args.binary.read_bytes()
 assert hashlib.sha256(binary).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 pe=struct.unpack_from('<I',binary,0x3c)[0];opt=pe+24
 base,size=[struct.unpack_from('<I',binary,opt+n)[0] for n in (28,56)]
 uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095);uc.mem_write(base,binary[:struct.unpack_from('<I',binary,opt+60)[0]])
 for i in range(struct.unpack_from('<H',binary,pe+6)[0]):
  s=opt+struct.unpack_from('<H',binary,pe+20)[0]+i*40;va,count,offset=struct.unpack_from('<III',binary,s+12)
  if count:uc.mem_write(base+va,binary[offset:offset+count])
 uc.mem_map(0x10000000,0x10000);uc.mem_map(0x20000000,0x10000)
 left,right,output,stack,stop=0x10000100,0x10000200,0x10000300,0x20008000,0x30000000
 write=lambda addr,*words:uc.mem_write(addr,struct.pack('<'+'I'*len(words),*(w&MASK for w in words)))
 read=lambda addr:struct.unpack('<I',uc.mem_read(addr,4))[0]
 startup=initialize_runtime_scalars(uc,stack,stop)
 assert list(struct.unpack('<13I',uc.mem_read(0x6fcd588c,52)))==C

 def call(entry,a,b=None,alias=0,budget=50000,expect_stop=True):
  destination=[output,left,right][alias]
  write(left-4,0xabc00101,a,0xabc00102);write(right-4,0xabc00201,b or 0,0xabc00202)
  write(output-4,0xabc00301,0xdeadbeef,0xabc00302)
  if entry==0x6f20f990:write(stack,stop,left,right)
  else:write(stack,stop,b if entry==0x6f071180 else right)
  uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_ECX,destination);uc.reg_write(UC_X86_REG_EDX,left)
  preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
  for i,reg in enumerate(preserved):uc.reg_write(reg,0xabc01000+i)
  loop=[]
  def observe_loop(machine,address,size,data):loop.append(machine.reg_read(UC_X86_REG_EBX))
  hook=uc.hook_add(UC_HOOK_CODE,observe_loop,begin=0x6f0711a0,end=0x6f0711a0) if not expect_stop else None
  if hook:uc.ctl_flush_tb()  # Earlier helper calls may have cached the loop block without this observer.
  uc.emu_start(entry,stop,count=budget)
  if hook:uc.hook_del(hook)
  if not expect_stop:
   assert uc.reg_read(UC_X86_REG_EIP)!=stop and loop[-3:]==[MASK]*3,(hex(entry),loop[-5:],hex(uc.reg_read(UC_X86_REG_EIP)))
   return dict(entry=hex(entry),base_word=a,exponent_word=b,budget=budget,loop_exponent_tail=loop[-3:],stopped_before_return=True)
  assert uc.reg_read(UC_X86_REG_EIP)==stop,('budget',hex(entry),hex(a),hex(b or 0))
  assert uc.reg_read(UC_X86_REG_ESP)==stack+(8 if entry in (0x6f0710e0,0x6f071180) else 4)
  assert [uc.reg_read(reg) for reg in preserved]==[0xabc01000+i for i in range(4)]
  for addr,sentinel in [(left-4,0xabc00101),(left+4,0xabc00102),(right-4,0xabc00201),(right+4,0xabc00202),(output-4,0xabc00301),(output+4,0xabc00302)]:assert read(addr)==sentinel
  if entry==0x6f20f990:return uc.reg_read(UC_X86_REG_EAX)
  assert uc.reg_read(UC_X86_REG_EAX)==destination
  if destination!=left:assert read(left)==a
  if destination!=right:assert read(right)==(b or 0)
  return read(destination)
 records=[]
 counts={}
 engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
 if engine:
  for name in ('corelog','reducedlog','log','exp','power','public_power'):
   fn=getattr(engine,'pathing_'+name);fn.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)];fn.restype=None
 def compare_c(name,a,b,actual):
  if not engine:return
  words=(ctypes.c_uint32*2)(0xabcdef01,0xabcdef02);getattr(engine,'pathing_'+name)(a,b or 0,words)
  assert list(words)==[1,actual],('C',name,hex(a),hex(b or 0),list(words),hex(actual))
 rng=random.Random(0x127190)
 rawlog=[rng.getrandbits(32) for _ in range(12000)]
 for a in rawlog:
  actual=call(0x6f070f70,a);expected=log(a)
  assert actual==expected,('raw-log',hex(a),hex(actual),hex(expected))
  records.append(['raw-log',a,actual]);compare_c('log',a,None,actual)
 counts['raw_log']=len(rawlog)

 for name,entry,model,inputs in [
  ('corelog',0x6f06fd50,corelog,[bits(x/100) for x in range(1,1001)]),
  ('reducedlog',0x6f06ff20,reducedlog,[bits(x/1000) for x in range(1,2001)]),
  ('log',0x6f070f70,log,[bits(x/37) for x in range(1,3001)]),
  ('exp-positive',0x6f06fe10,exp_positive,[bits(x/100) for x in range(0,1001)]),
  ('exp',0x6f070c20,exp,[bits(x/100) for x in range(-1000,1001)])]:
  for a in inputs:
   actual=call(entry,a);expected=model(a)
   assert actual==expected,(name,hex(a),value(a),hex(actual),hex(expected))
   records.append([name,a,actual])
   if name!='exp-positive':compare_c(name,a,None,actual)
  counts[name]=len(inputs)
 for a in [bits(x) for x in (0,-0.,.0005,-.0005,.001,-.001,.5,-.5,1,-1,2,-2,10,-10,100)]:
  for b in [bits(x) for x in (-3,-2,-1,-.5,-.001,-.0005,0,.0005,.001,.5,1,2,3,8)]:
   actual=call(0x6f20f990,a,b);expected=public(a,b)
   assert actual==expected,('Pow',value(a),value(b),hex(actual),hex(expected))
   records.append(['Pow',a,b,actual]);compare_c('public_power',a,b,actual)

 raw_exp_controls=[]
 # Raw exceptional exp words, including a wrapped negative scaled sub-unit.
 for a in [0xff920e3a,0x7f920e3a,0, SIGN,0x7f800000,0xff800000]+[rng.getrandbits(32) for _ in range(1200)]:
  try:expected=exp(a)
  except ValueError:
   raw_exp_controls.append(call(0x6f070c20,a,budget=6000,expect_stop=False))
   continue  # Recorded original nonreturn, no fabricated output.
  actual=call(0x6f070c20,a);assert actual==expected,('raw-exp',hex(a),hex(actual),hex(expected))
  compare_c('exp',a,None,actual);records.append(['raw-exp',a,actual])
 # Output/input aliasing at the raw helper ABI; public wrapper always owns locals.
 for name,entry,model in [('corelog',0x6f06fd50,corelog),('reducedlog',0x6f06ff20,reducedlog),('log',0x6f070f70,log),('exp',0x6f070c20,exp)]:
  for a in [bits(x/32) for x in range(1,257)]:
   for alias in (1,2):
    actual=call(entry,a,alias=alias);assert actual==model(a);records.append([name+'-alias',a,alias,actual])
 for a in [bits(x) for x in (-2,-.5,0,.5,1,2,10)]:
  for b in [bits(x) for x in (-3,-.5,0,.5,2,7)]:
   for alias in (0,1,2):
    actual=call(0x6f0710e0,a,b,alias);assert actual==power(a,b);compare_c('power',a,b,actual);records.append(['power-helper',a,b,alias,actual])
 # Explicit signed-SAR nontermination, observed without claiming any output.
 controls=[call(0x6f071180,bits(2),n,budget=6000,expect_stop=False) for n in (SIGN,MASK,SIGN+1)]
 controls += raw_exp_controls
 controls += [call(0x6f20f990,bits(2),bits(2147483648),budget=6000,expect_stop=False)]
 if engine:
  for name,a,b in [('power',bits(2),bits(2147483648)),('public_power',bits(2),bits(2147483648))]:
   words=(ctypes.c_uint32*2)(0xabcdef01,0xabcdef02);getattr(engine,'pathing_'+name)(a,b,words)
   assert list(words)==[0,0xabcdef02],('C-nontermination',name,list(words))
 report=dict(binary_sha256=hashlib.sha256(binary).hexdigest(),verified_calls=len(records),counts=counts,
  constants=C,original_startup_initializers=startup,nontermination_controls=controls,nontermination_control_count=len(controls),engine_compared=bool(engine),
  outcome_sha256=hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest(),
  scope='Original scalar log/exp/power and registered Pow wrappers; original registered -1/0/1 initializers executed from poisoned storage, immutable DLL constants/table validated. Independent integer scalar model, finite/public guards, sign/near-zero, raw log words, output aliases, ABI guards and nonvolatile registers. Signed negative integer-power exponents stop at declared instruction budgets with no numeric result. Public creation/compiled literals and original VM watchdog excluded.')
 args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=="__main__":main()
