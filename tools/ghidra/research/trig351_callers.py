import json,re
from collections import defaultdict
from pathlib import Path
def ledger(x):
 # Reaching output definitions follow both conditional branches. A linear scan
 # misclassifies nine matrix branches by reading definitions past an earlier RET.

 forward={'6f05cb30','6f05cb80','6f1d4d20','6f219590','6f2195a0','6f2195c0','6f219890'}
 rows=[]
 for t,refs in x['calls'].items():
  for r in refs:
   f=x['functions'][r['entry']];lines=f['assembly'].splitlines();ins={};pred=defaultdict(list)
   for line in lines:
    a,op,*tail=line.split(' ',2);ins[a]=(op,tail[0]if tail else'')
   for a,(op,args)in ins.items():
    next_addr=f'{int(a,16)+len(x["instructions"][a])//2:08x}'
    if op.startswith('RET'):continue
    if op.startswith('J'):
     if args.startswith('0x'):
      dest=f'{int(args,16):08x}'
      if dest in ins:pred[dest].append(a)
     if op=='JMP':continue
    if next_addr in ins:pred[next_addr].append(a)
   reg='EDX'if t=='6f071340'else'ECX'
   def definitions(site,register):
    seen=set();stack=list(pred[site]);defs=[]
    while stack:
     a=stack.pop()
     if a in seen:continue
     seen.add(a);op,args=ins[a]
     if args.split(',')[0]==register and op not in ('PUSH','TEST','CMP'):
      defs.append(a+' '+op+' '+args)
     elif op=='CALL'and register in ('EAX','ECX','EDX'):defs.append(a+' CALL-clobber')
     elif a==r['entry']:defs.append(a+' ENTRY-'+register)
     else:stack+=pred[a]
    return sorted(defs)
   def origins(site,register,visiting=frozenset()):
    key=(site,register)
    if key in visiting:raise ValueError('cyclic unresolved pointer origin')
    visiting=visiting|{key};result=set()
    for definition in definitions(site,register):
     address,kind,*tail=definition.split(' ',2)
     if kind.startswith('ENTRY-'):result.add(('external',kind));continue
     op,args=ins[address]
     if kind=='CALL-clobber':
      if register=='EAX'and args in {'0x6f06f9c0','0x6f06fbb0','0x6f06fa90','0x6f06fcd0'}:
       result.update(origins(address,'ECX',visiting));continue
      raise ValueError('unresolved pointer return '+definition)
     source=args.split(',',1)[1]
     if op=='MOV'and re.fullmatch('E[A-Z]{2}',source):
      result.update(origins(address,source,visiting));continue
     if op=='MOV'and ('ptr ['in source or source.startswith('0x')):
      result.add(('external',source));continue
     if op=='LEA':
      match=re.fullmatch(r'\[(E[A-Z]{2})(?: \+ (-?0x[0-9a-f]+))?\]',source)
      if not match:raise ValueError('unresolved address '+definition)
      base_reg,offset=match.groups();offset=int(offset or '0',16);offset=offset-0x100000000 if offset>=0x80000000 else offset
      if base_reg=='EBP':result.add(('frame',offset))
      else:
       for basis,value in origins(address,base_reg,visiting):
        result.add((basis,value+offset if basis=='frame'else str(value)+'+'+str(offset)))
      continue
     raise ValueError('unresolved pointer definition '+definition)
    if not result:raise ValueError('missing pointer origin '+site+' '+register)
    return result
   input_origins=[origins(r['site'],'ECX'if t=='6f071340'else'EDX')]
   output_origins=[origins(r['site'],reg)]
   if t in ('6f070530','6f071340'):
    # The last pushed word is the third pointer. All these callers push it
    # without another call/pop before the guarded scalar invocation.
    pending=list(pred[r['site']]);seen=set();third=set()
    while pending:
     address=pending.pop()
     if address in seen:continue
     seen.add(address);op,args=ins[address]
     if op=='PUSH':
      if re.fullmatch('E[A-Z]{2}',args):third.update(origins(address,args))
      elif 'ptr ['in args:third.add(('external',args))
      else:raise ValueError('unresolved stack pointer '+address)
     elif op in ('POP','CALL'):raise ValueError('unresolved third operand '+address)
     else:pending+=pred[address]
    if t=='6f070530':input_origins.append(third)
    else:output_origins.append(third)
   if r['entry']not in forward:
    if any(out&inp for out in output_origins for inp in input_origins):raise ValueError('concrete caller aliases trig input '+r['site'])
    if t=='6f071340'and r['entry']!='6f16e6c0'and output_origins[0]&output_origins[1]:raise ValueError('concrete paired outputs alias')
   defs=definitions(r['site'],reg)
   disposition='forwarded-addresses'if r['entry']in forward else 'value-angle-forwarded-outputs'if r['entry']=='6f16e6c0'else 'own-frame-output'
   if disposition=='own-frame-output':
    assert defs and all(re.fullmatch(r'\w+ LEA '+reg+r',\[EBP \+ (?:-?0x[0-9a-f]+)\]',s)for s in defs),(r,defs)
   i=next(i for i,l in enumerate(lines)if l.startswith(r['site']+' '))
   rows.append(dict(target=t,**r,disposition=disposition,input_origins=[[list(a)for a in sorted(v)]for v in input_origins],output_origins=[[list(a)for a in sorted(v)]for v in output_origins],output_definitions=defs,input_definitions=definitions(r['site'],'ECX'if t=='6f071340'else'EDX'),call_context=lines[max(0,i-12):i+1]))
 return rows

def witness(binary,library):
 """Actual guarded helpers/forwarders; forced aliases are not gameplay producers."""
 import ctypes as C,hashlib,struct
 import verify_wc3_pathing_numeric as n
 from generate_wc3_math_tables import sine_table,acos_tables,reciprocal_table
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import (UC_X86_REG_ESP as SP,UC_X86_REG_EIP as IP,
   UC_X86_REG_ECX as CX,UC_X86_REG_EDX as DX,UC_X86_REG_EAX as AX,
   UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)
 data=binary.read_bytes();assert hashlib.sha256(data).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 pe=struct.unpack_from('<I',data,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',data,opt+i)[0]for i in (28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',data,pe+6)[0]):
  s=opt+struct.unpack_from('<H',data,pe+20)[0]+40*i;va,count,off=struct.unpack_from('<III',data,s+12)
  if count:u.mem_write(base+va,data[off:off+count])
 u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
 stack,stop,buffer=0x20008000,0x30000000,0x10000100
 def wr(at,words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words)))
 def rd(at,count=1):return list(struct.unpack('<'+'I'*count,u.mem_read(at,count*4)))
 n.initialize_runtime_scalars(u,stack,stop)
 preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
 writes=[]
 def observe(machine,access,address,size,value,user):
  if buffer-16<=address<buffer+28:writes.append((address-buffer,size,value&0xffffffff))
 u.hook_add(UC_HOOK_MEM_WRITE,observe)
 a,b=acos_tables();rec=reciprocal_table();sines=sine_table()
 models={0x6ffa0:lambda x:n.acos_bits(x,a,b),0x703a0:lambda x:n.asin_bits(x,a,b),
  0x705b0:lambda x:n.atan_bits(x,rec),0x70790:lambda x:n.trig_bits(x,True,sines),
  0x71280:lambda x:n.trig_bits(x,False,sines),
  0x71590:lambda x:n.divide(n.trig_bits(x,False,sines),n.trig_bits(x,True,sines),rec)}
 def negative(w):return bool(w&0x80000000 and w&0x7fffffff)
 def run(entry,regs,args,cleanup):
  wr(stack,[stop,*args]);u.reg_write(SP,stack)
  for reg,v in regs.items():u.reg_write(reg,v)
  for reg in preserved:u.reg_write(reg,0xabba0000+reg)
  u.emu_start(base+entry,stop,count=200000)
  assert u.reg_read(IP)==stop and u.reg_read(SP)==stack+4+cleanup,hex(entry)
  assert all(u.reg_read(reg)==0xabba0000+reg for reg in preserved),hex(entry)
 words=[0,0x80000000,0x1,0x80000001,0x3a83126e,0xba83126e,0x3a83126f,0xba83126f,
  0x3e8930a2,0xbe8930a2,0x3e8930a3,0xbe8930a3,0x3f000000,0xbf000000,
  0x3f7e8000,0xbf7e8000,0x3f7e8001,0xbf7e8001,0x3f800000,0xbf800000,
  0x3fc90fdb,0xbfc90fdb,0x40490fdb,0xc0490fdb,0x40c90fdb,0xc0c90fdb,
  0x4b800001,0xcb800001,0x7f7fffff,0xff7fffff]
 records=[];controls=0;engine=C.CDLL(str(library.resolve()))
 helper_names={0x6ffa0:'acos',0x703a0:'asin',0x705b0:'atan',0x70790:'cos',0x71280:'sin',0x71590:'tan'}
 wrappers={0x6ffa0:0x1d4d20,0x703a0:0x219590,0x705b0:0x2195c0,0x70790:0x05cb30,0x71280:0x05cb80,0x71590:0x219890}
 for helper,forward in wrappers.items():
  fn=getattr(engine,'pathing_'+helper_names[helper]);fn.restype=C.c_uint32
  for word in words:
   for alias in (False,True):
    out=buffer if alias else buffer+4
    model_input=word&0x7fffffff if alias and helper in (0x6ffa0,0x703a0)else word
    expected=models[helper](model_input)
    if helper==0x705b0:
     expected=models[helper](word&0x7fffffff)
     if negative(expected if alias else word):expected^=0x80000000
    if not alias:assert fn(C.c_uint32(word))==expected
    outputs=[]
    for entry in (helper,forward):
     wr(buffer-16,[0xabcdef01]*4+[word,0xabcdef02,0xabcdef03]+[0xabcdef04]*4);writes.clear()
     run(entry,{CX:out,DX:buffer},[],0)
     assert u.reg_read(AX)==out
     memory=rd(buffer-16,11);assert memory[4+(out-buffer)//4]==expected,(hex(entry),hex(word),alias,memory,hex(expected))
     assert memory[:4]==[0xabcdef01]*4 and memory[7:]==[0xabcdef04]*4
     assert all(address==out-buffer and size==4 for address,size,value in writes)
     outputs.append(memory)
    assert outputs[0]==outputs[1]
    if alias and expected!=models[helper](word):controls+=1
    records.append([helper,word,alias,expected,outputs[0]])
 # Every equality partition of output/y/x; initialize shared sources only once.
 partitions=[(0,1,2),(0,0,1),(0,1,0),(0,1,1),(0,0,0)]
 for word in words:
  for other in (0,0xbf000000,0x3f000000,0xbf800000):
   for output,y,x in partitions:
    before=[0xabcdef02]*3;before[y]=word
    if x!=y:before[x]=other
    expected=before.copy();yy,xx=before[y],before[x]
    result=n.atan_bits(n.divide(yy,xx,rec)&0x7fffffff,rec)if xx&0x7f800000 else 0x3fc90fdb
    expected[output]=result
    if negative(expected[x]):result=n.subtract(0x40490fdb,result);expected[output]=result
    if negative(expected[y]):result^=0x80000000;expected[output]=result
    for entry in (0x70530,0x2195a0):
     wr(buffer-16,[0xabcdef01]*4+before+[0xabcdef04]*4);writes.clear()
     run(entry,{CX:buffer+output*4,DX:buffer+y*4},[buffer+x*4],4)
     assert u.reg_read(AX)==buffer+output*4
     assert rd(buffer,3)==expected,(hex(entry),before,(output,y,x),rd(buffer,3),expected)
     assert rd(buffer-16,4)==[0xabcdef01]*4 and rd(buffer+12,4)==[0xabcdef04]*4
     assert all(at==output*4 and size==4 for at,size,value in writes)
    if output not in (y,x):
     engine.pathing_atan2.restype=C.c_uint32
     assert engine.pathing_atan2(C.c_uint32(yy),C.c_uint32(xx))==result
    records.append([0x70530,word,other,[output,y,x],expected])
 # Paired helper: angle is captured before either output store; cosine wins a shared output.
 for word in words:
  sine=n.trig_bits(word,False,sines);cosine=n.trig_bits(word,True,sines)
  for angle,s,c in partitions:
   before=[0xabcdef02]*3;before[angle]=word;expected=before.copy();expected[s]=sine;expected[c]=cosine
   wr(buffer-16,[0xabcdef01]*4+before+[0xabcdef04]*4);writes.clear()
   run(0x71340,{CX:buffer+angle*4,DX:buffer+s*4},[buffer+c*4],4)
   assert rd(buffer,3)==expected
   assert all(at in (s*4,c*4)and size==4 for at,size,value in writes)
   records.append([0x71340,word,[angle,s,c],expected])
  for shared in (False,True):
   wr(buffer-16,[0xabcdef01]*4+[0xabcdef02]*3+[0xabcdef04]*4);writes.clear()
   run(0x16e6c0,{CX:buffer,DX:buffer if shared else buffer+4},[word],4)
   assert rd(buffer,3)==([cosine,0xabcdef02,0xabcdef02]if shared else[sine,cosine,0xabcdef02])
   records.append([0x16e6c0,word,shared,rd(buffer,3)])
 return dict(cases=len(records),alias_sign_controls=controls,outcome_sha256=hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest())
