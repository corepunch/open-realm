"""Original Storm574 metadata float producer and game729f24 SSE cap slice.

No native callee replacement. Storm's real table startup also runs unchanged.
The six-byte caller return shim stores the x87 float result for inspection.
"""
from pathlib import Path
import hashlib,struct,random,json,argparse
if __package__:
 from .supportgoal_ramp_oracle import OriginalRamp,prepared_grid
else:
 from supportgoal_ramp_oracle import OriginalRamp,prepared_grid
STORM_SHA='36339f69727f0f3dcb4431a9567b84f56007c331cfa3b183862af43e8314eb72'
class Metadata:
 def __init__(self,binary,storm):
  self.e=e=OriginalRamp(binary);raw=Path(storm).read_bytes()
  if hashlib.sha256(raw).hexdigest()!=STORM_SHA:raise ValueError('wrong Storm')
  pe=struct.unpack_from('<I',raw,60)[0];o=pe+24;base,size=[struct.unpack_from('<I',raw,o+n)[0]for n in(28,56)]
  if base!=0x15000000:raise ValueError('Storm base differs')
  e.u.mem_map(base,(size+4095)&~4095)
  for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
   at=o+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,at+12)
   if n:e.u.mem_write(base+va,raw[off:off+n])
  e.call(0x1503a390,[])
 def run(self,text):
  e=self.e;u=e.u;x=e.X;string=0x10020000
  u.mem_write(string,text.encode('latin1')+b'\0');e.write(e.stack,e.stop+0x600,string)
  u.mem_write(e.stop+0x600,b'\xd9\x1d'+struct.pack('<I',e.output)+b'\xe9'+struct.pack('<i',-0x60b))
  u.reg_write(x.UC_X86_REG_ESP,e.stack);u.reg_write(x.UC_X86_REG_FPCW,0x27f);u.reg_write(x.UC_X86_REG_FPSW,0);u.reg_write(x.UC_X86_REG_FPTAG,0xffff)
  u.emu_start(0x1503b140,e.stop,count=1000000)
  if u.reg_read(x.UC_X86_REG_EIP)!=e.stop or u.reg_read(x.UC_X86_REG_ESP)!=e.stack+8:raise ValueError('Storm ABI/budget differs '+repr(text)+' EIP='+hex(u.reg_read(x.UC_X86_REG_EIP))+' ESP='+hex(u.reg_read(x.UC_X86_REG_ESP)))
  value=struct.unpack('<I',u.mem_read(e.output,4))[0]
  e.write(e.terrain+0x980,value);u.reg_write(x.UC_X86_REG_EBX,e.terrain);u.reg_write(x.UC_X86_REG_EBP,e.stack+0x100)
  u.mem_write(e.stop+0x100,b'\xd9\xee');u.emu_start(e.stop+0x100,e.stop+0x102,count=1)
  u.emu_start(0x6f729f24,0x6f729f72,count=1000)
  if u.reg_read(x.UC_X86_REG_EIP)!=0x6f729f72:raise ValueError('cap slice budget differs')
  cap=struct.unpack('<I',u.mem_read(e.terrain+0x988,4))[0]
  return dict(text=text,value=value,cap=cap)
def cases():
 out=['','0','-0','+1536',' 1536','1536','-256','0e99999','1.536e3','1536e+0','1536e-0','-2.56E2','274877906944','549755813888','-549755813888','1e400','-1e400','nan','inf','0.00000000000001','0.123456789123456789123456789','429496729','429496728','4294967295','123456789012345678901234567890']
 out += [str(n+d) for n in range(-256,4097,128) for d in(-1,0,1)]
 out += [f'{n}.0000000000000001' for n in range(-256,4097,128)]
 rng=random.Random(574)
 for i in range(512):
  digits=''.join(str(rng.randrange(10))for _ in range(rng.randrange(1,40)))
  at=rng.randrange(len(digits)+1);text=digits[:at]+'.'+digits[at:]
  if i%2:text='-'+text
  if i%3==0:text+='e'+str(rng.randrange(-400,401))
  out.append(text)
 return out
def original(binary,storm,ramps):
 e=Metadata(binary,storm);rows=[e.run(text) for text in cases()]
 index=next(i for i,row in enumerate(ramps) if row['offset']==0x42000000)
 fields=[]
 for text in ['0e99999','-2.56E2','1.536e3','+1536','274877906944','1e400','-1e400','1536']:
  row=e.run(text);fields.append(dict(row,index=index,values=prepared_grid(binary,ramps[index],row['cap'])))
 return dict(storm_sha256=STORM_SHA,cases=rows,fields=fields)

def render(data):
 out=['/* Literal unchanged Storm574 and game729f24 producer results. */',
      'static struct {char const *text;uint32_t value,cap;} const support_metadata_cases[]={']
 for row in data['cases']:out.append('    {%s,0x%08xu,0x%08xu},'%(json.dumps(row['text']),row['value'],row['cap']))
 out+=['};','static struct {char const *text;uint32_t index,cap,values[36];} const support_metadata_fields[]={']
 for row in data['fields']:out.append('    {%s,%d,0x%08xu,{'%(json.dumps(row['text']),row['index'],row['cap'])+','.join('0x%08xu'%v for v in row['values'])+'}},')
 return '\n'.join(out+['};',''])

if __name__=='__main__':
 import gzip
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--storm',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 root=Path(__file__).resolve().parents[3]
 ramps=json.loads(gzip.decompress((root/'tools/ghidra/fixtures/retail-supportgoal-ramp-kernels-1.27.json.gz').read_bytes()))['cases']
 data=original(a.binary,a.storm or a.binary.parent/'Storm.dll',ramps)
 a.report.write_text(json.dumps(data,indent=2)+'\n');print('native metadata cases',len(data['cases']),'actual fields',len(data['fields']))
