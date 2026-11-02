"""Execute native Captain selector and relocation wrapper; terminal placement is supplied.

Identity payloads and placement completion are controlled. This verifies the
original producer/selector ABI, not a substitute for real placement or motion.
"""
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE

def original(binary):
 rows=[]
 for x,y in [(0,0),(0x80000000,0),(0xc4f20000,0xc4480000),(0xc4c40c00,0xc40c0800),
             (0xc5090000,0xc4280000),(0x42f68000,0xc37c4000)]:
  for finish in [False,True]:
   e=Emu(binary);ctx=e.fixture(0x100);town=e.fixture(0x300);captain=e.fixture(0x200)
   xp=e.fixture(8);yp=e.fixture(8);lookups=[];calls=[]
   e.w(0x6fd77ecc,ctx);e.w(ctx+0x2c,5,7);e.w(town+0x2ac,9,11);e.w(town+0x2b8,12,13)
   e.w(xp,x,0x41414141);e.w(yp,y,0x42424242)
   def hook(uc,address,size,data):
    sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX)
    if address==0x6f061320:
     identity=e.r(ecx,2);lookups.append(identity)
     if identity==[5,7]:value=town
     elif identity==[9,11]:value=captain
     else:raise ValueError('unexpected identity')
     pop=0
    else:
     args=e.r(sp+4,11)
     if ecx!=captain or args[:2]==[xp,yp]:raise ValueError('wrong actor/input alias')
     words=[e.r(args[0]),e.r(args[1])]
     calls.append(dict(xy=words,arguments=args[2:]))
     if finish:e.w(args[0],0x43000000);e.w(args[1],0x43800000)
     value=0x43800000 if finish else y;pop=44
    uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
   for addr in [0x6f061320,0x6f653510]:e.uc.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
   e.call(0x6f9c0490,0,xp,yp)
   if lookups!=[[5,7],[9,11]]or calls!=[dict(xy=[x,y],arguments=[1,1,0xffffffff,0,0,0,0,0xffffffff,0])]or e.r(xp)!=x or e.r(yp)!=y or e.esp_after!=4:raise ValueError('native wrapper differs')
   rows.append(dict(xy=[x,y],controlled_placement_writes=finish,lookups=lookups,calls=calls,stack=e.esp_after))
 return rows
