#!/usr/bin/env python3
"""Compose original clock/elapsed and FIFO instructions at an epoch boundary.

Starting clock, owner cadence and charged work are supplied controls. Public
rollover/motion and backward UI load remain independently verified by the
unchanged Payoff117/181 captures; this controlled oracle does not replace them.
"""
import argparse, hashlib, importlib.util, json, struct
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('requests261',HERE/'verify_ORDER-05.1_request_heap.py')
requests=importlib.util.module_from_spec(spec);spec.loader.exec_module(requests)
SHA=requests.SHA

def execute(binary):
    from unicorn.x86_const import UC_X86_REG_EDX
    case=requests.mk('clock-and-path-fifos','SCHED-01.2',1,[],
        clocks=[requests.clock(time=requests.bits(299.875)),requests.clock()],wrapper_spec=[{'repeat':True}])
    original=requests.Original(binary,case);m=original.m
    def op(value):
        b=requests.o31.CodeBuilder(m);original.ops(b,[value]);b.b+=b'\xc3';m.call(b.done(),0)
    op(('start',0,requests.bits(.125),7))
    # Complete scheduler initializer, excluding unrelated CRT atexit registration.
    m.u.reg_write(m.R['ESP'],requests.o31.STACK+0xf000)
    m.u.emu_start(0x6f003d20,0x6f0040c0,count=10000)
    assert m.u.reg_read(m.R['EIP'])==0x6f0040c0
    buckets=[0x6fd53a90+k*0x1c for k in range(4)]
    paths=[[m.obj(0xa0)for _ in range(3)]for _ in range(4)]
    for k,bucket in enumerate(buckets):m.w(bucket+8,[801,301,901,1101][k],[3,2,2,1][k])
    clock=original.clocks[0];out=m.obj(4);old=m.obj(8);delta=m.obj(4);m.w(delta,requests.bits(.005))
    point=[requests.bits(4),requests.bits(4)];velocity=[requests.bits(.25),requests.bits(.125)]
    scalar=requests.Scalar(binary);counter=1000;rows=[]
    def identity(k,p):return paths[k].index(p)+1 if p else 0
    for step in range(1,81):
        m.w(old,m.r(clock+0x40),m.r(clock+0x44))
        m.u.reg_write(UC_X86_REG_EDX,clock);m.call(requests.ADVANCE,delta)
        m.call(0x6f15cea0,clock,out,clock+0x40,m.r(clock+0x44),old,m.r(old+4))
        elapsed=m.r(out)
        point=[scalar.add(p,scalar.op(0x6f06f9c0,v,elapsed))for p,v in zip(point,velocity)]
        row=dict(step=step,time=m.r(clock+0x40),epoch=m.r(clock+0x44),elapsed=elapsed,point=list(point),counter=counter,buckets=[])
        if step%6==0:
            counter+=1;m.call(0x6f167310,0);row['counter']=counter
            for k,bucket in enumerate(buckets):
                decisions=[0]*3
                for i in (2,0,1):
                    path=paths[k][i]
                    admitted=m.call(0x6f168910,path,1 if k==3 else 0,counter,out)
                    if admitted:
                        admitted=m.call(0x6f168310,bucket,path)
                        if not admitted:m.w(path+(0x80 if k==3 else 0x7c),0)
                        else:m.w(bucket+8,m.r(bucket+8)+[801,301,901,1101][k])
                    decisions[i]=admitted
                row['buckets'].append([m.r(bucket+8),m.r(bucket+12),m.r(bucket+16),
                    identity(k,m.r(bucket+20)),identity(k,m.r(bucket+24)),*decisions,
                    *[m.r(path+(0x80 if k==3 else 0x7c))for path in paths[k]]])
        rows.append(row)
    assert rows[24]['epoch']==0 and rows[25]['epoch']==1
    assert original.state()['clocks'][1]['time']==requests.T0
    return dict(binary_sha256=SHA,rows=rows,requests=original.log,final=original.state(),
        limits=['Controlled starting time299.875, six-advance owner visits, three requesters and supplied over-budget charge per admitted search.',
            'Original elapsed and scalar integration execute; no full original mover/route or public owner cadence is claimed by this oracle.',
            'Public natural rollover and backward UI load are separately covered by unchanged Payoff117/181 evidence.'])

def header(result):
    text='/* Unmodified retail261 clock/FIFO controls; public evidence is separate. */\n'
    text+='static struct {uint32_t time,epoch,elapsed,point[2],counter,bucket[4][11];} const boundary261[]={\n'
    for row in result['rows']:
        word=lambda x:'0x%08xu'%x
        blocks=row['buckets']or [[0]*11 for _ in range(4)]
        text+=' {'+','.join(map(word,[row['time'],row['epoch'],row['elapsed']]))+',{'+','.join(map(word,row['point']))+'},'+word(row['counter'])+',{'+','.join('{'+','.join(map(word,b))+'}'for b in blocks)+'}},\n'
    return text+'};\n'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--header',type=Path);p.add_argument('--expected',type=Path);a=p.parse_args()
    if a.report.exists():p.error('new report required')
    binary=a.binary.read_bytes();assert hashlib.sha256(binary).hexdigest()==SHA
    result=execute(binary)
    if a.expected:assert result==json.loads(a.expected.read_text()),'original boundary evidence changed'
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n')
    if a.header:
        if a.header.exists():assert a.header.read_text()==header(result),'existing header differs'
        else:a.header.write_text(header(result))
    print(json.dumps(dict(passed=True,advances=len(result['rows']),owner_visits=sum(bool(r['buckets'])for r in result['rows']),wraps=result['rows'][-1]['epoch'])))
if __name__=='__main__':main()
