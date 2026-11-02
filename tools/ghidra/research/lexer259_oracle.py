#!/usr/bin/env python3
"""Original JASS numeric DFA boundaries, with host input/EOF and memmove only.

Stops at924bc8 after original token text/length writes, before action dispatch.
Numeric controls run the complete parent and original conversion action.
Identifier interning/compiler behavior is covered separately by live captures.
"""
import argparse,itertools,json,random,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from foot03_spatial_harness_copy import Emu
from verify_wc3_pathing_numeric import initialize_runtime_scalars

class Lexer:
 def __init__(self,binary):
  from unicorn import UC_HOOK_CODE
  self.e=e=Emu(binary);self.unit=e.fixture(0x100);vt=e.fixture(16)
  self.text=e.fixture(32768);self.states=e.fixture(32768);self.stop_action=True
  e.w(self.unit,vt);e.w(vt,0x100ff000,0x100ff010);e.w(self.unit+8,self.states);e.w(self.unit+12,32768);e.w(self.unit+0x98,self.text)
  e.w(0x6fa7c508,0x100ff020)
  initialize_runtime_scalars(e.uc,e.stack,e.stop)
  def hook(uc,a,size,data):
   X=e.X;sp=uc.reg_read(X.UC_X86_REG_ESP)
   if a in(0x100ff000,0x100ff010,0x100ff020):
    if a==0x100ff020:
     dst,src,n=e.r(sp+4,3);uc.mem_write(dst,bytes(uc.mem_read(src,n)));value=dst
    else:value=0xffffffff if a==0x100ff000 else 1
    uc.reg_write(X.UC_X86_REG_EAX,value);uc.reg_write(X.UC_X86_REG_ESP,sp+4);uc.reg_write(X.UC_X86_REG_EIP,e.r(sp))
   elif a==0x6f924bc8 and self.stop_action:uc.emu_stop()
  e.uc.hook_add(UC_HOOK_CODE,hook)
 def first(self,text,complete=False):
  e=self.e;raw=text.encode('latin1');assert len(raw)<32000 and b'\0'not in raw
  e.uc.mem_write(self.text,raw+b'\0');e.w(self.unit+0x20,raw[0]if raw else 0)
  e.w(self.unit+0x10,len(raw));e.w(self.unit+0xc4,0);e.w(self.unit+0x18,10);e.w(self.unit+0xc0,1)
  e.w(e.stack,e.stop);e.uc.reg_write(e.X.UC_X86_REG_ESP,e.stack);e.uc.reg_write(e.X.UC_X86_REG_ECX,self.unit)
  self.stop_action=not complete;e.uc.emu_start(0x6f9249d0,e.stop,count=5000000)
  ip=e.uc.reg_read(e.X.UC_X86_REG_EIP)
  if ip not in(e.stop,0x6f924bc8):raise ValueError('lexer instruction bound')
  length=e.r(self.unit+0xc4);token=bytes(e.uc.mem_read(self.text,length)).decode('latin1')
  if complete:
   if ip!=e.stop or e.uc.reg_read(e.X.UC_X86_REG_ESP)!=e.stack+4:raise ValueError('complete parent ABI')
   return dict(token=e.uc.reg_read(e.X.UC_X86_REG_EAX),text=token,length=length,word=e.r(self.unit+0x24))
  return dict(action=e.uc.reg_read(e.X.UC_X86_REG_EBX)&511,text=token,length=length)if ip!=e.stop else dict(action=None,text='',length=0)

def numeric_states(e):
 # Same base/check/next/default lookup as924ab0, with the jam state231.
 def transition(state,ch):
  for _ in range(232):
   at=e.r(0x6fcf27e8+state*4)+ch
   if at<=0x872 and bytes(e.uc.mem_read(0x6fcf1e88+at,1))[0]==state:
    return bytes(e.uc.mem_read(0x6fcf1610+at,1))[0]
   if state==231:return 231
   state=bytes(e.uc.mem_read(0x6fcf2700+state,1))[0]
  raise ValueError('DFA fallback cycle')
 prefixes={};pending=[]
 for ch in '$.0123456789':
  state=transition(0,ord(ch))
  if state!=231 and state not in prefixes:prefixes[state]=ch;pending.append(state)
 for state in pending:
  for ch in range(256):
   nxt=transition(state,ch)
   if nxt!=231 and nxt not in prefixes:
    prefixes[nxt]=prefixes[state]+chr(ch);pending.append(nxt)
 result=[]
 for state in sorted(prefixes):
  lo,hi=e.r(0x6fcf1270+state*4,2)
  actions=[e.r(0x6fcf0e50+i*4)for i in range(lo,hi)]
  if any(a>>9 for a in actions):raise ValueError('unexpected numeric trailing context')
  result.append(dict(state=state,prefix=prefixes[state],actions=actions,
   transitions=[transition(state,ch)for ch in range(256)]))
 return result

def original(binary):
 lex=Lexer(binary);states=numeric_states(lex.e);inputs=['0','077','078','08','09','089.5','01.2','1.','0.','.5','.','..5',
  '1e3','1.2e3','1.2E-3','nan','inf','NaN','Infinity','nan(1)','nan1','infinite',
  '0x','0X','0xG','0Xf','0xf','0XFF','0x12.5','0x1p4','$','$G','$aF','$12.5','1.2.3',
  '-1.5','+1.5','851983then','0078then','0.5then','0Xfthen']
 for prefix in dict.fromkeys(['0','07','1','12.','0x','0X','$','.',*[s['prefix']for s in states]]):
  inputs.extend(prefix+chr(c)+'9'for c in range(1,256))
 rng=random.Random(259)
 for _ in range(512):inputs.append(''.join(rng.choice('012789abcdefxX.$eE+-_')for _ in range(rng.randrange(1,40))))
 rows=[]
 for text in dict.fromkeys(inputs):
  row=dict(input=text,**lex.first(text));rows.append(row)
  if row['action']in(42,43,44,45,46):
   control=lex.first(text,True)
   if control['length']!=row['length']or control['text']!=row['text']or control['token']!=(265 if row['action']==46 else 264):raise ValueError('numeric parent differs')
   row['word']=control['word']
 return dict(rows=rows,numeric_states=states,limits=[__doc__.strip()])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 r=original(a.binary);a.report.write_text(json.dumps(r,indent=2)+'\n');print(len(r['rows']))
if __name__=='__main__':main()
