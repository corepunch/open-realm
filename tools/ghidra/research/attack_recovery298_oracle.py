#!/usr/bin/env python3
import argparse,json,struct,itertools,hashlib,copy
from pathlib import Path
from foot03_spatial_harness_copy import Emu,SHA256
W=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]

ROOT=Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.json'

def run(binary):
    from unicorn import UC_HOOK_CODE
    e=Emu(binary);X=e.X
    A,U,T,V,TV,B,N=[e.fixture(n) for n in [0x500,0x400,0x400,0x200,0x200,0x100,0x100]]
    owner_stub,bridge_stub=e.fixture(0x10),e.fixture(0x10)
    e.w(U,V);e.w(T,TV,10);e.w(V+0xec,owner_stub);e.w(TV+0xb8,bridge_stub)
    e.w(A+0x30,U);e.w(A+0x2bc,0xffffffff,0xffffffff)
    e.w(0x6fd3c7ac,0xdeadbeef)
    e.call(0x6f001cd0)
    assert e.r(0x6fd3c7ac)==W(50)
    state={};calls=[];tasks=[]
    def hook(uc,addr,size,data):
     sp=uc.reg_read(X.UC_X86_REG_ESP);cx=uc.reg_read(X.UC_X86_REG_ECX);result=0;argc=0
     if addr==owner_stub:result=0
     elif addr==bridge_stub:assert cx==T;result=B
     elif addr==0x6f061320:
      assert cx in (A+0x6c,A+0x78)
      result=(T if state['target']!='null' else 0) if cx==A+0x6c else (T if state['alternative']!='stale' else 0)
     elif addr==0x6f1d73b0:
      arg=e.r(sp+4);e.w(cx,arg if state['target']!='widget' else 0);argc=1
      if e.r(cx):e.w(T+4,e.r(T+4)+1)
     elif addr==0x6f05aa20:
      assert cx==B;e.w(e.r(sp+4),W(state['radius']));result=e.r(sp+4);argc=1
     elif addr==0x6f058900:
      assert cx==B;e.w(e.r(sp+4),*map(W,state['position']));result=e.r(sp+4);argc=1
     elif addr==0x6f499790:
      assert cx==A;slot=e.r(sp+4);assert slot<2;result=int(state['enabled'][slot]);argc=1
     elif addr==0x6f405510:calls.append('clear_active_ownership_service')
     elif addr==0x6f497e20:calls.append('release_target_windup_service')
     elif addr==0x6f497110:assert e.r(sp+4,2)==[U,1];argc=2
     elif addr==0x6f4968e0:
      assert e.r(sp+4,3)==[T,1,0];result=int(state['alternative']=='invalid');argc=3
     elif addr==0x6f694620:assert e.r(sp+4)==2;argc=1
     elif addr==0x6f69a840:calls.append('stop_service')
     elif addr==0x6f69c6a0:
      assert cx==U;assert tasks;e.w(N+0x30,tasks[0]);result=N
     elif addr==0x6f691260:
      assert cx==U;calls.append({'pop':tasks.pop(0)})
     elif addr==0x6f67e130:calls.append('cancel_completion')
     elif addr==0x6f692120:
      event,px,py,pr,owner=e.r(sp+4,5);assert cx==U
      calls.append({'point_words':[event,e.r(px),e.r(py),e.r(pr),owner]});argc=5
     elif addr==0x6f67df00:calls.append('dispatch')
     else:raise AssertionError(hex(addr))
     uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4+4*argc);uc.reg_write(X.UC_X86_REG_EIP,e.r(sp))
    services=[owner_stub,bridge_stub,0x6f061320,0x6f1d73b0,0x6f05aa20,0x6f058900,0x6f499790,0x6f405510,0x6f497e20,0x6f497110,0x6f4968e0,0x6f694620,0x6f69a840,0x6f69c6a0,0x6f691260,0x6f67e130,0x6f692120,0x6f67df00]
    for a in services:e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
    cases=cases_spec()
    rows=[]
    range_output=e.fixture(4)
    for idx,c in enumerate(cases):
     state=c;tasks[:]=c['chain'];calls.clear()
     flags=(4 if c['active'] else 0)|(0x40000 if c['loss'] else 0)
     e.w(A+0x20,flags);e.w(A+0x6c,3,7);e.w(A+0x78,9,11)
     e.w(A+0x258,W(c['ranges'][0]));e.w(A+0x260,W(c['ranges'][1]));e.w(U+0x20,0x200 if c['source200'] else 0);e.w(U+0x60,0)
     e.w(T+0x20,0x800000 if c['blink'] else 0);e.w(T+4,10)
     e.call(0x6f4985c0,A,range_output,budget=10000)
     maximum_word=e.r(range_output)
     e.call(0x6f49d280,A,c['forced'],c['capture'],budget=100000)
     row=dict(case=idx,maximum_range_word=maximum_word,range_input_words=list(map(W,c['ranges'])),inputs=copy.deepcopy(c),flags_after=e.r(A+0x20),alternative_after=e.r(A+0x78,2),chain_after=tasks[:],services=copy.deepcopy(calls),stack_delta=e.esp_after,references_after=e.r(T+4))
     rows.append(row)
    # Execute original pop helper separately; no scripted pop decision.
    poprows=[]
    for chain in [[0xd016a,0xd016b],[0xd0169,0xd016b],[0xd0168,0xd0163,0xd016f,0xd016b],[0xd0163,0xd016f,0xd016b],[0xd016f,0xd016b],[0xd0168,0xd016b],[0xd016b]]:
     tasks[:]=chain;calls.clear();result=e.call(0x6f497190,A,budget=10000)
     poprows.append(dict(before=chain,result=result,after=tasks[:],services=copy.deepcopy(calls),stack_delta=e.esp_after))
    neg=negative_controls(rows)
    result=dict(original_sha256=SHA256,original_functions=['001cd0','49d280','497190','4985c0','06fbb0'],scope=['Original producer, real task-pop branch and two-slot maximum selection execute unmodified.','Synthetic memory and labelled external identity/class/weapon-eligibility/worldposition/radius/ownership/taskqueue services; no public reachability or native scheduler claim.','RuntimeFifty is initialized by unmodified001cd0 after poisoning its output; original06fbb0 arithmetic executes. Adjusted-range targetidentity invalid as ordinary public initialization; no adjusted2cc branch claim.','Release/stop services deliberately retain flag4 to exercise postcallback branch; no callback mutation/reentrancy claim.'],rows=rows,pop_rows=poprows,negative_controls=neg)
    result['initializer']={'entry_rva':'0x001cd0','output_va':'0x6fd3c7ac','poison_before':0xdeadbeef,'word_after':e.r(0x6fd3c7ac)}
    validate(result)
    return result

def cases_spec():
    base=dict(active=True,forced=0,capture=0,loss=True,source200=False,target='unit',blink=False,alternative='valid',ranges=[90.,500.],enabled=[True,True],radius=31.,position=[1234.25,987.125,0.],chain=[0xd0168,0xd0163,0xd016f,0xd016b])
    cases=[]
    for active,forced,capture,loss,source200,blink in itertools.product([False,True],repeat=6):
     c=base.copy();c.update(active=active,forced=int(forced),capture=int(capture),loss=loss,source200=source200,blink=blink);cases.append(c)
    for key,values in [('target',['null','widget']),('alternative',['invalid','stale']),('enabled',[[True,False],[False,True],[False,False]]),('chain',[[0xd0169,0xd016b],[0xd016a,0xd016b],[0xd016b],[0xd016f,0xd016b],[0xd0168,0xd016b]])]:
     for value in values:c=base.copy();c[key]=value;cases.append(c)
    for bits in [(0x43a68000,0x43a68000),(0x43a67fff,0x43a68000),(0x43a68000,0x43a67fff),(0x43a68000,0x43a68001),(0x43a68001,0x43a68000)]:
     c=base.copy();c['ranges']=[struct.unpack('<f',struct.pack('<I',b))[0] for b in bits];cases.append(c)
    return cases

def validate_rows(rows):
 cases=cases_spec()
 assert len(rows)==len(cases)
 for idx,(r,c) in enumerate(zip(rows,cases)):
  assert r['maximum_range_word']==W(max([0.]+[v for v,en in zip(c['ranges'],c['enabled']) if en]))
  assert r['range_input_words']==list(map(W,c['ranges']))
  assert r['case']==idx and r['inputs']==c and r['stack_delta']==12 and r['references_after']==10
  chain=c['chain'][:];seq=[]
  if c['active']:
   seq=['clear_active_ownership_service','release_target_windup_service','stop_service']
   popped=False
   if chain[0]==0xd0169:seq.append({'pop':chain.pop(0)})
   elif chain[0]!=0xd016a:
    if chain[0]==0xd0168:seq.append({'pop':chain.pop(0)})
    if chain[0]==0xd0163:seq+=['cancel_completion',{'pop':chain.pop(0)}]
    if chain[0]==0xd016f:seq.append({'pop':chain.pop(0)});popped=True
   capture=(c['capture'] or(c['loss'] and not c['source200'])) and c['target']!='null' and(not c['blink'] or c['target']=='widget')
   if popped:
    if capture:
     rang=max([0.]+[v for v,en in zip(c['ranges'],c['enabled']) if en])+c['radius']+50
     seq.append({'point_words':[0xd016c,W(c['position'][0]),W(c['position'][1]),W(rang),0]})
    seq.append('dispatch')
  assert r['services']==seq,(idx,r['services'],seq)
  assert r['chain_after']==chain
  assert r['flags_after']==(0 if c['active'] else (0x40000 if c['loss'] else 0))
  assert r['alternative_after']==([0xffffffff]*2 if c['active'] and(c['forced'] or c['alternative']=='invalid') else [9,11])

def negative_controls(rows):
    neg=[]
    for kind in ['missing','extra','reorder','range_word','point_word','event_word','force_clear','flags','task_pop']:
     q=copy.deepcopy(rows)
     if kind=='missing':q.pop()
     elif kind=='extra':q.append(q[0])
     elif kind=='reorder':q[0],q[1]=q[1],q[0]
     else:
      r=next(r for r in q if any(isinstance(v,dict) and 'point_words'in v for v in r['services']))
      if kind in ('range_word','point_word','event_word'):
       p=next(v['point_words'] for v in r['services'] if isinstance(v,dict) and 'point_words'in v);p[{'range_word':3,'point_word':1,'event_word':0}[kind]]^=1
      elif kind=='force_clear':r['alternative_after']=[0xffffffff]*2
      elif kind=='flags':r['flags_after']^=0x40000
      elif kind=='task_pop':r['chain_after']=[]
     try:validate_rows(q)
     except AssertionError:neg.append(kind);continue
     raise AssertionError('mutation accepted '+kind)
    return neg

POP_CHAINS=[[0xd016a,0xd016b],[0xd0169,0xd016b],[0xd0168,0xd0163,0xd016f,0xd016b],[0xd0163,0xd016f,0xd016b],[0xd016f,0xd016b],[0xd0168,0xd016b],[0xd016b]]

def validate(data):
    """Validate frozen inputs and every output word without Unicorn or retail files."""
    assert data['original_sha256']==SHA256
    assert data['original_functions']==['001cd0','49d280','497190','4985c0','06fbb0']
    assert data['initializer']=={'entry_rva':'0x001cd0','output_va':'0x6fd3c7ac','poison_before':0xdeadbeef,'word_after':0x42480000}
    validate_rows(data['rows'])
    assert len(data['pop_rows'])==7
    for r,chain,(count,result) in zip(data['pop_rows'],POP_CHAINS,[(0,0),(1,0),(3,1),(2,1),(1,1),(1,0),(0,0)]):
        assert r['before']==chain and r['result']==result and r['after']==chain[count:] and r['stack_delta']==4
        expected=[]
        for code in chain[:count]:
            if code==0xd0163:expected.append('cancel_completion')
            expected.append({'pop':code})
        assert r['services']==expected
    assert data['negative_controls']==['missing','extra','reorder','range_word','point_word','event_word','force_clear','flags','task_pop']

def verify_provenance(root=ROOT):
    spec=json.loads((root/'games/warcraft-3/game/tests/fixtures/retail_attack_recovery298.provenance.json').read_text())
    for path,digest in spec['repository_files'].items():
        assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
    return spec

def main():
    parser=argparse.ArgumentParser(description="Original F19 recovery decision oracle with labelled synthetic external services.")
    parser.add_argument('--binary',required=True)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    data=run(args.binary)
    if args.check:
        assert data==json.loads(FIXTURE.read_text()), 'original output differs from frozen fixture'
        verify_provenance()
    if args.report: args.report.write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',cases=len(data['rows']),pop_cases=len(data['pop_rows']),negative_controls=data['negative_controls'])))

if __name__=='__main__': main()
