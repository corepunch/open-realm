#!/usr/bin/env python3
"""Original531a80 refresh oracle; no live game or public/scheduler parity claim."""
import argparse, copy, hashlib, json, struct
from pathlib import Path
from foot03_spatial_harness_copy import Emu, SHA256
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/research/item-invisibility322-original.json'
W=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]
def cases():
    return [(n,W(v)) for n,v in [('shorter',4),('equal',5),('longer',6),('zero',0),('negative',-1)]]
def validate(rows):
    assert len(rows)==5
    for row,(name,word) in zip(rows,cases()):
        assert row['case']==name and row['requested_word']==word and row['remaining_word']==W(5)
        expected=['v1b4','v1c8','context324']
        if name in ('zero','negative'):expected+=['cancel'];flag=0x400
        else:
            expected+=['remaining'];flag=0x480
            if name=='longer':expected+=['selector348','rearm34c']
        assert row['calls']==expected and row['flags_after']==flag and row['stack_delta']==12
        assert row['rearm_word']==(word if name=='longer' else None)
def negative_controls(rows):
    mutations=[]
    a=copy.deepcopy(rows);a.pop();mutations.append(a)
    a=copy.deepcopy(rows);a.append(a[-1]);mutations.append(a)
    a=copy.deepcopy(rows);a[0],a[1]=a[1],a[0];mutations.append(a)
    a=copy.deepcopy(rows);a[1]['calls'].append('rearm34c');mutations.append(a)
    a=copy.deepcopy(rows);a[2]['rearm_word']^=1;mutations.append(a)
    a=copy.deepcopy(rows);a[3]['flags_after']|=128;mutations.append(a)
    for a in mutations:
        try:validate(a)
        except AssertionError:continue
        raise AssertionError('mutation accepted')
    return len(mutations)
def run(binary):
    from unicorn import UC_HOOK_CODE
    e=Emu(binary);X=e.X
    e.call(0x6f001c30,0,budget=10000);assert e.r(0x6fd3c74c)==W(2)
    a,v,tv,d,ctx=[e.fixture(n) for n in (0x100,0x400,0x30,4,0x10)]
    names=['v1b4','v1c8','context324','remaining','selector348','rearm34c']
    stubs={e.fixture(16):n for n in names};addr={n:p for p,n in stubs.items()}
    e.w(a,v);e.w(a+0x84,tv)
    for off,n in [(0x1b4,'v1b4'),(0x1c8,'v1c8'),(0x324,'context324'),(0x348,'selector348'),(0x34c,'rearm34c')]:e.w(v+off,addr[n])
    e.w(tv+0x18,addr['remaining']);calls=[];rearm=[]
    def hook(uc,pc,size,data):
        sp=uc.reg_read(X.UC_X86_REG_ESP);cx=uc.reg_read(X.UC_X86_REG_ECX);n=stubs.get(pc,'cancel');calls.append(n);argc=0;ret=0
        if n in ('remaining','cancel'):assert cx==a+0x84
        else:assert cx==a
        if n=='context324':assert e.r(sp+4)==ctx;argc=1
        elif n=='remaining':ret=e.r(sp+4);e.w(ret,W(5));argc=1
        elif n=='selector348':ret=0xd01d5
        elif n=='rearm34c':assert e.r(sp+4,2)==[d,0xd01d5];rearm.append(e.r(d));argc=2
        uc.reg_write(X.UC_X86_REG_EAX,ret);uc.reg_write(X.UC_X86_REG_ESP,sp+4+argc*4);uc.reg_write(X.UC_X86_REG_EIP,e.r(sp))
    for pc in [*stubs,0x6f0606c0]:e.uc.hook_add(UC_HOOK_CODE,hook,begin=pc,end=pc)
    rows=[]
    for name,word in cases():
        calls.clear();rearm.clear();e.w(a+0x20,0x480);e.w(d,word)
        e.call(0x6f531a80,a,ctx,d,budget=10000)
        rows.append(dict(case=name,requested_word=word,remaining_word=W(5),calls=calls[:],flags_after=e.r(a+0x20),rearm_word=rearm[0] if rearm else None,stack_delta=e.esp_after))
    validate(rows);return rows

def byte_guards(binary,spec):
    b=Path(binary).read_bytes();assert hashlib.sha256(b).hexdigest()==SHA256
    p=struct.unpack_from('<I',b,60)[0];o=p+24+struct.unpack_from('<H',b,p+20)[0]
    sections=[struct.unpack_from('<III',b,o+40*i+12)for i in range(struct.unpack_from('<H',b,p+6)[0])]
    for g in spec['guards']:
        r=int(g['rva'],16);f=next(f+r-v for v,n,f in sections if v<=r<v+n);raw=bytes.fromhex(g['bytes']);assert b[f:f+len(raw)]==raw

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    spec=json.loads(FIXTURE.read_text());validate(spec['rows']);n=negative_controls(spec['rows'])
    if args.binary:
        byte_guards(args.binary,spec);assert run(args.binary)==spec['rows']
    print(json.dumps({'passed':True,'cases':5,'negative_controls':n,'original_executed':bool(args.binary),'scope':'Original refresh decision with labelled external services; no scheduler/public capture.'}))
if __name__=='__main__':main()
