"""Basic scalar alias/ABI contracts. Raw controls are distinct from public producers."""
import ctypes as C,hashlib,itertools,json,math,random,struct
from pathlib import Path
import verify_wc3_pathing_numeric as n
from generate_wc3_math_tables import reciprocal_table

def operators():
 r=reciprocal_table()
 return {
  0x6fbb0:('add',n.add,'binary'),0x6fa90:('subtract',n.subtract,'binary'),
  0x6f9c0:('multiply',n.multiply,'binary'),0x6fcd0:('divide',lambda a,b:n.divide(a,b,r),'binary'),
  0x70fe0:('modulo',lambda a,b:n.modulo(a,b,r),'binary'),
  0x70120:('integer_wrapped',n.integer_word,'read'),0x70170:('saturating_integer',n.saturating_integer_word,'read'),
  0x70d80:('integer_float',n.integer_float,'from-int'),0x71530:('integer_sqrt',math.isqrt,'value'),
  0x6fd30:('leading_zeroes',lambda a:32-a.bit_length(),'value'),
  0x70700:('ceil',n.ceil_word,'unary'),0x70c80:('floor',n.floor_word,'unary'),
  0x70d20:('fractional',n.fractional,'unary'),0x711e0:('reciprocal',lambda a:n.reciprocal(a,r),'unary'),
  0x71250:('round',n.round_word,'unary'),0x71480:('sqrt',n.square_root,'unary'),
  0x715c0:('truncate',n.truncate_word,'unary'),
  0x71570:('reciprocal_sqrt',lambda a:n.reciprocal(n.square_root(a),r),'unary'),
  0x70f20:('u32_power',lambda a,b:pow(a,b,1<<32),'two-values')}

def normalize(rows):
 """Retain counters, aliases and cross-call pointer reuse; normalize process addresses."""
 tokens={};outputs={};result=[];base=int(next(r for r in rows if r['event']=='module')['base'],16)
 def token(p):
  if p not in tokens:tokens[p]=len(tokens)
  return tokens[p]
 for row in rows:
  if row['event']not in ('marker','caller-enter','caller-leave','scalar-enter','scalar-leave','format-enter','format-leave','point-order','getter'):continue
  row={k:v for k,v in row.items()if k!='seq'}
  if row['event']=='scalar-enter':
   ni=len(row['inputs']);outputs[row['id']]=row['pointers'][ni:];sp=int(row.pop('sp'),16)
   row['locations']=[int(p,16)-sp if abs(int(p,16)-sp)<65536 else('global:'+hex(int(p,16)-base)if base<=int(p,16)<base+15130624 else'external')for p in row['pointers']]
   row['pointers']=[token(p)for p in row['pointers']]
  elif row['event']=='scalar-leave'and outputs[row['id']]:
   if row['result']!=int(outputs[row['id']][0],16):raise ValueError('scalar ABI return pointer differs')
   row['result']='output0'
  result.append(row)
 return result

def compare_scalars(rows,library):
 lib=C.CDLL(str(library.resolve()));ops=operators();starts={r['id']:r for r in rows if r['event']=='scalar-enter'};completed=set();counts={}
 for after in rows:
  if after['event']!='scalar-leave':continue
  before=starts[after['id']];name,model,kind=ops[before['entry']];args=before['inputs']or before['values'];expected=model(*args)
  actual=after['outputs'][0]if after['outputs']else after['result']
  if expected!=actual:raise ValueError(('original/model',name,args,actual,expected))
  f=getattr(lib,'pathing_'+name);f.restype=C.c_uint32
  if f(*(C.c_uint32(a)for a in args))!=expected:raise ValueError(('engine/model',name,args,expected))
  ni=len(before['inputs']);eq=before['equality'];out=before['pointers'][ni:]
  if after['c']!=before['c']:raise ValueError('scalar call advanced clock')
  for i,word in enumerate(after['inputs']):
   expect=expected if out and eq[ni][i]else before['inputs'][i]
   if word!=expect:raise ValueError('aliased input store differs')
  alias=','.join(str(i)for i in range(ni)if out and eq[ni][i])or'distinct'
  key=name+':'+alias;counts[key]=counts.get(key,0)+1;completed.add(after['id'])
 if set(starts)!=completed:raise ValueError('unmatched scalar lifetime')
 return dict(scalars=len(starts),contexts=counts)

def aliases(binary,library):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn.x86_const import (UC_X86_REG_ESP as SP,UC_X86_REG_EIP as IP,
  UC_X86_REG_ECX as CX,UC_X86_REG_EDX as DX,UC_X86_REG_EAX as AX,
  UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)
 raw=binary.read_bytes();assert hashlib.sha256(raw).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+i)[0]for i in (28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,count,off=struct.unpack_from('<III',raw,s+12)
  if count:u.mem_write(base+va,raw[off:off+count])
 u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
 stack,stop,buffer=0x20008000,0x30000000,0x10000100
 def wr(at,words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words)))
 n.initialize_runtime_scalars(u,stack,stop);preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
 rng=random.Random(352);words=[0,0x80000000,1,0x80000001,0x7fffff,0x807fffff]
 for e,m,s in itertools.product([1,2,103,126,127,128,149,150,157,158,181,254,255],[0,1,0x400001,0x7fffff],[0,0x80000000]):words.append(s|(e<<23)|m)
 words +=[rng.getrandbits(32)for _ in range(256)]
 partitions=[(8,0,4),(0,0,4),(4,0,4),(8,0,0),(0,0,0),(1,0,4),(3,0,4),(2,0,5),(5,0,4),(6,0,4),(0,1,5),(0,2,3),(4,0,1),(4,1,0)]
 lib=C.CDLL(str(library.resolve()));records=[];counts={};partial=0
 for entry,(name,model,kind)in operators().items():
  f=getattr(lib,'pathing_'+name);f.restype=C.c_uint32;count=0
  for i,word in enumerate(words):
   layouts=partitions if kind=='binary'else[(8,0,4),(0,0,4)]if kind in ('unary','from-int')else[(8,0,4)]
   for output,left,right in layouts:
    payload=bytearray(b'\xa5'*48);struct.pack_into('<I',payload,16+left,word);other=words[(i*17+5)%len(words)]
    if right!=left:struct.pack_into('<I',payload,16+right,other)
    a,b=(struct.unpack_from('<I',payload,16+p)[0]for p in (left,right))
    args=[a,b]if kind in ('binary','two-values')else[a];expected=model(*args);engine=f(*(C.c_uint32(v)for v in args));assert engine==expected,(name,args,engine,expected)
    u.mem_write(buffer-16,bytes(payload));regs={CX:buffer+output,DX:buffer+left};stack_args=[]
    if kind=='binary':stack_args=[buffer+right]
    elif kind=='read':regs={CX:buffer+left}
    elif kind=='value':regs={CX:a}
    elif kind=='two-values':regs={CX:a,DX:b}
    elif kind=='from-int':regs={CX:buffer+output,DX:a}
    wr(stack,[stop,*stack_args]);u.reg_write(SP,stack)
    for reg,value in regs.items():u.reg_write(reg,value)
    for reg in preserved:u.reg_write(reg,0x12340000+reg)
    u.emu_start(base+entry,stop,count=20000)
    assert u.reg_read(IP)==stop and u.reg_read(SP)==stack+4+len(stack_args)*4
    assert all(u.reg_read(reg)==0x12340000+reg for reg in preserved)
    if kind in ('read','value','two-values'):assert u.reg_read(AX)==expected
    else:
     assert u.reg_read(AX)==buffer+output
     struct.pack_into('<I',payload,16+output,expected)
    assert bytes(u.mem_read(buffer-16,len(payload)))==bytes(payload),(name,args,(output,left,right))
    count+=1
    if kind=='binary'and(output,left,right)not in partitions[:5]:partial+=1
    records.append([entry,args,[output,left,right],expected])
  counts[name]=count
 return dict(cases=len(records),partial_overlap_cases=partial,counts=counts,outcome_sha256=hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest())

def public_window(rows):
 """Only the declared synchronous tick2 callback; broad query traces are diagnostic."""
 marks=[(i,r)for i,r in enumerate(rows)if r['event']=='marker']
 start=next(i for i,r in marks if ' label=case=0 'in r['value'])
 end=max(i for i,r in marks if ' label=retained 'in r['value'])
 window=rows[start:end+1]
 if {r['c']for r in window if 'c'in r}!={1030}:raise ValueError('public window counter differs')
 complete={r['id']:r for r in rows if r['event']=='caller-enter'}
 inside={r['id']for r in window if r['event']=='caller-enter'}
 external={r['parent']for r in window if r['event']=='scalar-enter'}-inside
 contexts=[complete[i]for i in sorted(external)]
 for ctx in contexts:
  if ctx['entry']!=0x91b460 or ctx['c']!=1030:raise ValueError('unexpected public producer')
 normalized=normalize([next(r for r in rows if r['event']=='module'),*window]);ids={}
 def logical(i):
  if i is None:return None
  if i not in ids:ids[i]=len(ids)
  return ids[i]
 result=[]
 for row in contexts+normalized:
  row=dict(row)
  for key in ('id','parent','outer'):
   if key in row:row[key]=logical(row[key])
  row.pop('seq',None);result.append(row)
 return result

def caller_storage(inv):
 """Classify all basic direct roots, without a name-based movement filter.

 CFG reaching definitions preserve all branches, entry forwarding and call
 boundaries. The primitive overlap witness covers every possible byte relation;
 no opaque external pointer is asserted disjoint from another pointer.
 """
 from collections import defaultdict
 graphs={};result=[]
 basic={f'{0x6f000000+a:08x}':(name,kind)for a,(name,model,kind)in operators().items()}
 basic['6f070de0']=('decimal','text')
 for target,(name,kind)in basic.items():
  for ref in inv['calls'][target]:
   entry=ref['entry']
   if entry not in graphs:
    ins={};pred=defaultdict(list)
    for line in inv['functions'][entry]['assembly'].splitlines():
     a,op,*tail=line.split(' ',2);ins[a]=(op,tail[0]if tail else'')
    for a,(op,args)in ins.items():
     if op.startswith('RET'):continue
     if op.startswith('J'):
      if args.startswith('0x'):
       dest=f'{int(args,16):08x}'
       if dest in ins:pred[dest].append(a)
      if op=='JMP':continue
     nxt=f'{int(a,16)+len(inv["instructions"][a])//2:08x}'
     if nxt in ins:pred[nxt].append(a)
    graphs[entry]=(ins,pred)
   ins,pred=graphs[entry];site=ref['site'];op,args=ins[site]
   def reaching(register):
    pending=list(pred[site]);seen=set();definitions=[]
    if site==entry:definitions.append(entry+' ENTRY-'+register)
    while pending:
     a=pending.pop()
     if a in seen:continue
     seen.add(a);opcode,operands=ins[a]
     if opcode=='CALL':definitions.append(a+' CALL-volatile-'+register+' '+operands)
     elif operands.split(',')[0]==register and opcode not in ('PUSH','TEST','CMP'):
      definitions.append(a+' '+opcode+' '+operands)
     elif a==entry:definitions.append(a+' ENTRY-'+register)
     else:pending+=pred[a]
    if not definitions:raise ValueError('missing basic register definition '+site)
    return sorted(definitions)
   if op not in ('CALL','JMP'):
    result.append(dict(target=target,**ref,name=name,kind='address-registration'));continue
   reads=['ECX']if kind in ('read','value')else['ECX','EDX']if kind=='two-values'else['EDX']
   writes=[]if kind in ('read','value','two-values')else['ECX']
   result.append(dict(target=target,**ref,name=name,kind=kind,
    inputs={reg:reaching(reg)for reg in reads},outputs={reg:reaching(reg)for reg in writes},
    stack_input='original [ESP+4] pointer'if kind=='binary'else None,
    result='EAX value'if not writes else'EAX output pointer; scalar stores',
    relationship='all byte overlaps equivalent with original operand snapshots and composed store order'if kind not in ('text','read','value','two-values')else'text consumed before store'if kind=='text'else'value result; no destination store'))
 return result
