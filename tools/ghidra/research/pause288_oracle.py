"""Original transient-dispatch body; endpoints only record ABI/store order."""
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE
from pathlib import Path
def original(binary):
 rows=[]
 for dead in [False,True]:
  for depth in [0,1,2,0xffffffff]:
   for bypass in [0,1]:
    for handled in [0,1]:
     e=Emu(binary);u=e.fixture(0x300);o=e.fixture(0x80);v=e.fixture(0x100);release=e.fixture(4);e.uc.mem_write(release,b'\xc3')
     e.w(o,v);e.w(v+0x1c,0x6f684a70);e.w(v+0x5c,release);e.w(u+0x5c,0x100 if dead else 0);e.w(u+0x198,depth);calls=[]
     endpoints={0x6f67bd10:('notify',1,0),0x6f691c70:('retain',3,0),0x6f679cc0:('execute',1,handled),0x6f67c070:('fallback',1,0),release:('release',0,0)}
     def hook(uc,a,size,data):
      name,n,result=endpoints[a];sp=uc.reg_read(e.X.UC_X86_REG_ESP);args=[e.r(sp+4+4*i) for i in range(n)]
      if uc.reg_read(e.X.UC_X86_REG_ECX)!=(o if name=='release' else u):raise ValueError('receiver')
      if args!=([o,1,1] if name=='retain' else [o] if n else []):raise ValueError('args')
      calls.append([name,e.r(o+0x20)]);uc.reg_write(e.X.UC_X86_REG_EAX,result);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+4*n)
     for a in endpoints:e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
     e.call(0x6f688790,u,o,bypass)
     expected=[['notify',0]]
     if dead:expected+=[['release',0]]
     elif 0<depth<0x80000000 and not bypass:expected+=[['retain',2]]
     else:
      expected+=[['execute',2]]
      if not handled:expected+=[['fallback',2]]
      expected+=[['release',2]]
     if calls!=expected or e.esp_after!=12:raise ValueError((calls,expected,e.esp_after))
     rows.append(dict(dead=dead,depth=depth,bypass=bypass,handled=handled,calls=calls,stack=e.esp_after))
 return rows

def counters(binary):
 rows=[]
 for kind in ['add','remove','retire']:
  for depth in [0,1,2]:
   for order_depth in [0,1,2]:
    for orders in ([0,1] if kind!='retire' else [0]):
     for delta in ([1,2] if kind!='retire' else [1]):
      e=Emu(binary);unit=e.fixture(0x300);a=e.fixture(0x100);v=e.fixture(0x400)
      e.w(a,v);e.w(a+0x30,unit);e.w(a+0x3c,depth);e.w(a+0x40,order_depth)
      for offset,address in [(0xcc,0x6f3fa480),(0xe0,0x6f407470),(0xe4,0x6f40b850),(0x2f4,0x6f42bca0),(0xc4,0x6f605aa0)]:e.w(v+offset,address)
      calls=[];endpoints={0x6f605aa0:'off',0x6f401fa0:'refresh',0x6f0606c0:'cancel'}
      def hook(uc,address,size,data):
       name=endpoints[address];sp=uc.reg_read(e.X.UC_X86_REG_ESP)
       if uc.reg_read(e.X.UC_X86_REG_ECX)!=(a+0x58 if name=='cancel' else a):raise ValueError('counter receiver')
       calls.append([name,e.r(a+0x3c),e.r(a+0x40)])
       uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4)
      for address in endpoints:e.uc.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
      if kind=='retire':e.call(0x6f3f4c70,a)
      else:e.call(0x6f4076e0 if kind=='add' else 0x6f40bb00,a,orders,delta)
      new_depth=(depth+(-delta if kind=='remove' else delta))&0xffffffff
      new_order=(order_depth+(-delta if kind=='remove' else delta)*orders)&0xffffffff
      expected=[]
      if kind!='remove' and not depth:expected.append(['off',new_depth,order_depth])
      expected.append(['refresh',new_depth,new_order])
      if kind=='retire':expected.extend([['off',new_depth,new_order],['cancel',new_depth,new_order]])
      if calls!=expected or e.r(a+0x3c)!=new_depth or e.r(a+0x40)!=new_order or e.esp_after!=(4 if kind=='retire' else 12):raise ValueError((kind,calls,expected))
      rows.append(dict(kind=kind,depth=depth,order_depth=order_depth,orders=orders,delta=delta,calls=calls,final_depth=new_depth,final_order_depth=new_order,flags=e.r(a+0x20)))
 return rows
