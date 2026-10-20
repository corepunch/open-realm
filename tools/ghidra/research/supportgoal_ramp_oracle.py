#!/usr/bin/env python3
"""Execute original ramp topology/offset instructions without replacement callees.

Synthetic packed vertices supply only their native index, layer and ramp bit.
The arithmetic, classification, cached high-layer flag and security cookies all
run from the SHA-checked original PE. This supplements real map-start captures;
it does not replace journey or producer/lifecycle acceptance.
"""
import argparse, hashlib, json, random, struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

class OriginalRamp:
    def __init__(self,binary):
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
        from unicorn import x86_const as X
        raw=Path(binary).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('wrong original PE')
        self.X=X;self.u=Uc(UC_ARCH_X86,UC_MODE_32)
        pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
        base,size=[struct.unpack_from('<I',raw,opt+n)[0]for n in(28,56)]
        self.u.mem_map(base,(size+4095)&~4095)
        for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
            s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i
            va,n,offset=struct.unpack_from('<III',raw,s+12)
            if n:self.u.mem_write(base+va,raw[offset:offset+n])
        self.u.mem_map(0x10000000,0x100000)
        self.stop,self.stack,self.terrain,self.vertices,self.output=[0x10000000+n for n in(0,0x80000,0x1000,0x4000,0x3000)]
        for fn in(0x6f017c10,0x6f017c30,0x6f017c40):self.call(fn,[])

    def write(self,at,*words):self.u.mem_write(at,struct.pack('<'+'I'*len(words),*(w&0xffffffff for w in words)))
    def call(self,entry,args):
        x=self.X;u=self.u;self.write(self.stack,self.stop,*args)
        u.reg_write(x.UC_X86_REG_ESP,self.stack);u.reg_write(x.UC_X86_REG_ECX,self.terrain)
        u.reg_write(x.UC_X86_REG_FPCW,0x27f);u.reg_write(x.UC_X86_REG_FPSW,0);u.reg_write(x.UC_X86_REG_FPTAG,0xffff)
        u.emu_start(entry,self.stop,count=1000000)
        if u.reg_read(x.UC_X86_REG_EIP)!=self.stop or u.reg_read(x.UC_X86_REG_ESP)!=self.stack+4+4*len(args):raise ValueError('original ramp ABI/budget differs')
        return u.reg_read(x.UC_X86_REG_EAX)

    def run(self,layers,ramps,point=(3,3),cap=14):
        width=7
        if len(layers)!=width*width or len(ramps)!=width*width:raise ValueError('expected7x7 packed input')
        data=bytearray(width*width*28)
        for i,(layer,ramp) in enumerate(zip(layers,ramps)):
            struct.pack_into('<I',data,i*28+4,i);data[i*28+10]=layer;data[i*28+12]=2 if ramp else 0
        self.write(self.terrain+0xb4,width-1,width-1)
        self.write(self.terrain+0xe4,self.vertices);self.write(self.terrain+0x988,cap)
        self.u.mem_write(self.vertices,bytes(data))
        kind=self.call(0x6f744750,list(point));typ=self.call(0x6f7445c0,list(point))&255
        corner=self.call(0x6f73a380,list(point));self.call(0x6f7446e0,[self.output,*point])
        result=struct.unpack('<I',self.u.mem_read(self.output,4))[0]
        return dict(kind=kind,type=typ,corner=corner,offset=result)


def cases():
    rng=random.Random(4042);out=[]
    # Exhaust the central3x3 ramp mask across straight and corner layer bands.
    for shape in range(6):
      for ox in (2,3,4):
       for oy in (2,3,4):
        layers=[2+int((y>=oy) if shape==0 else (x>=ox) if shape==1 else
            (x>=ox and y>=oy) if shape==2 else (x>=ox or y>=oy) if shape==3 else
            (x+y>=ox+oy) if shape==4 else (x>=ox)!=(y>=oy)) for y in range(7) for x in range(7)]
        for mask in range(512):
            ramps=[False]*49
            for dx in range(3):
                for dy in range(3):ramps[(dy+2)*7+dx+2]=bool(mask&(1<<(dx*3+dy)))
            out.append(dict(layers=layers,ramps=ramps,point=[3,3],cap=14))
    for _ in range(768):
        out.append(dict(layers=[rng.randrange(1,5) for _ in range(49)],ramps=[bool(rng.getrandbits(1)) for _ in range(49)],
            point=[rng.randrange(7),rng.randrange(7)],cap=rng.choice([0,1,2,3,14])))
    return out


def original(binary,inputs=None):
    engine=OriginalRamp(binary)
    return [dict(case,**engine.run(case['layers'],case['ramps'],case['point'],case['cap']))for case in (cases() if inputs is None else inputs)]



def render(rows):
    out=['/* SHA-checked original744750/7445c0/73a380/7446e0 instructions; no replacement callees. */',
         'static struct {uint64_t layers[2],ramps;uint8_t x,y;int32_t cap;uint32_t kind,type,corner,offset;} const preparedwater_ramp_kernels[]={']
    for row in rows:
        layers=sum((v-1)<<(i*2) for i,v in enumerate(row['layers']))
        ramps=sum(int(v)<<i for i,v in enumerate(row['ramps']))
        out.append('    {{0x%016xull,0x%016xull},0x%016xull,%d,%d,%d,0x%08xu,0x%08xu,0x%08xu,0x%08xu},'%(
            layers&((1<<64)-1),layers>>64,ramps,*row['point'],row['cap'],row['kind'],row['type'],row['corner'],row['offset']))
    return '\n'.join(out+['};',''])


def replay_capture(binary,records,cap):
    """Replay natural producer corner calls on the captured packed map input."""
    engine=OriginalRamp(binary)
    entries=[r for r in records if r.get('event')=='water-build-enter']
    if len(entries)!=1:raise ValueError('expected one native prepared-map input')
    entry=entries[0];width,height=entry['width'],entry['height'];vertices=entry['vertices']
    if len(vertices)!=(width+1)*(height+1):raise ValueError('packed vertex count differs')
    engine.write(engine.terrain+0xb4,width,height)
    engine.write(engine.terrain+0xe4,engine.vertices);engine.write(engine.terrain+0x988,cap)
    engine.u.mem_write(engine.vertices,b''.join(struct.pack('<IIII',*v)+bytes(12) for v in vertices))
    count=0
    for row in records:
        if row.get('event') not in ('ramp-offset','ground-corner'):continue
        address=0x6f7446e0 if row['event']=='ramp-offset' else 0x6f7451f0
        engine.call(address,[engine.output,row['x'],row['y']])
        value=struct.unpack('<I',engine.u.mem_read(engine.output,4))[0]
        if value!=row['value']:raise ValueError('original prepared corner differs from live capture '+str(row))
        count+=1
    return count


def prepared_grid(binary,case,cap):
    """Run748ab0 unchanged on a dry synthetic map with preallocated grid storage.

    Native vector capacity/count are sufficient, so its allocation branch is
    naturally unnecessary; no allocation or scalar callee is replaced.
    """
    e=OriginalRamp(binary);e.run(case['layers'],case['ramps'],case['point'],cap)
    for i in range(49):e.write(e.vertices+i*28,8192)
    grid=0x10010000;e.write(e.terrain+0x77c,36,36,grid,0)
    e.write(e.terrain+0x30c,0)
    e.call(0x6f748ab0,[])
    return list(struct.unpack('<36I',e.u.mem_read(grid,36*4)))


def render_fields(rows):
    out=['static struct {uint32_t index;int32_t cap,maximum;uint32_t values[36];} const preparedwater_ramp_fields[]={']
    for row in rows:
        out.append('    {%d,%d,%d,{'%(row['index'],row['cap'],row['maximum'])+','.join('0x%08xu'%v for v in row['values'])+'}},')
    return '\n'.join(out+['};',''])


def authored_fields(binary,rows):
    out=[]
    for offset in (0x42000000,0x42c00000,0x42800000):
        index=next(i for i,row in enumerate(rows) if row['offset']==offset)
        for cap,maximum in [(14,1536),(0,-256),(2,0)]:
            out.append(dict(index=index,cap=cap,maximum=maximum,values=prepared_grid(binary,rows[index],cap)))
    return out

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);args=p.parse_args()
    if args.report.exists():p.error('fresh report required')
    rows=original(args.binary);fields=authored_fields(args.binary,rows)
    args.report.write_text(json.dumps(dict(game_sha256=SHA,cases=rows,prepared_grids=fields),separators=(',',':'))+'\n')
    from collections import Counter
    print('original ramp inputs',len(rows),'offsets',Counter(hex(c['offset']) for c in rows),'authored fields',len(fields))
