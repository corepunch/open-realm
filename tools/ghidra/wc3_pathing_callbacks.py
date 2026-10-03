"""Original callback-boundary requests; never replace code or virtual callbacks."""
import struct


def invoke_preserving_context(machine, call):
    """Run original instructions on another stack, retaining only memory effects."""
    from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,
                                  UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EIP)
    def read(address):return struct.unpack('<I',machine.mem_read(address,4))[0]
    saved=machine.context_save()
    exception=read(0)
    arguments=call.get('arguments',[])
    stack=call['stack'];stop=call['stop']
    try:
        words=[stop]+arguments
        machine.mem_write(stack,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
        machine.reg_write(UC_X86_REG_ESP,stack);machine.reg_write(UC_X86_REG_ECX,call['receiver'])
        machine.reg_write(UC_X86_REG_EDX,call.get('edx',0))
        preserved={r:machine.reg_read(r) for r in (UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)}
        machine.emu_start(call['entry'],stop,count=2000000)
        assert machine.reg_read(UC_X86_REG_EIP)==stop,('callback producer budget',hex(call['entry']))
        assert machine.reg_read(UC_X86_REG_ESP)==stack+4+4*len(arguments),('callback producer stack',hex(call['entry']))
        assert all(machine.reg_read(r)==v for r,v in preserved.items()),('callback producer registers',hex(call['entry']))
        assert read(0)==exception,('callback producer exception chain',hex(call['entry']))
        result=machine.reg_read(UC_X86_REG_EAX)
    finally:
        machine.context_restore(saved)
    return result


def reuse_member(machine, context, run):
    """Release/reallocate a real mover while the group's slot54 callback is paused."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
    owner,group,registry,inputs,stack,stop=(context[k] for k in ('owner','group','registry','inputs','stack','stop'))
    movers=context['movers'];spec=context['spec']
    victim=spec['victim'];trigger=spec['trigger'];actor=movers[victim]
    members=read(group+0x28)[0]
    before_rows=[read(members+0x2c*n,11) for n in range(2)]
    assert read(group+0x38)==[2]
    identity=read(actor+0x14,2);path=read(actor+0xa8)[0];objects=read(actor+0x94,2)
    old_handles=[read(obj+0x14,2) for obj in [actor,path]+objects]
    pool=owner+0x7d8
    path_pool=owner+0x958
    before_path_pool=read(path_pool+0x14,3)
    assert before_path_pool[1]==3  # Two owned paths and the active group path.
    before=dict(registry_live=read(registry+0x48)[0],mover_pool=read(pool+0x14,3),
                spatial_pool=read(owner+0x5d8+0x14,3),generation_next=read(registry+0x50)[0])
    # Recycled spare objects are allocator backing, as in the existing baseline.
    # Retired spatial objects can retain lazy map records and cannot be reused
    # until those records release their references. Do not force their counters.
    spare=[0x10558004,0x10558104]
    assert read(owner+0x5d8+0x14)==[0]
    for n,obj in enumerate(spare):
        run(0x6f14c1d0,obj);write(obj-4,spare[1]-4 if n==0 else 0)
    write(owner+0x5d8+0x14,spare[0]-4)
    pending=True;callbacks=[]
    def boundary(uc,address,length,data):
        nonlocal pending
        callback_actor=uc.reg_read(UC_X86_REG_ECX)
        if pending and callback_actor==movers[trigger]:
            pending=False;uc.emu_stop()
        else:callbacks.append(dict(role=movers.index(callback_actor),identity=read(callback_actor+0x14,2)))
    hook=machine.hook_add(UC_HOOK_CODE,boundary,begin=0x6f16fa00,end=0x6f16fa00)
    write(stack,stop);machine.reg_write(UC_X86_REG_ESP,stack);machine.reg_write(UC_X86_REG_ECX,group)
    machine.emu_start(0x6f16bc10,stop,count=2000000)
    assert not pending and machine.reg_read(UC_X86_REG_EIP)==0x6f16fa00
    def call(request):
        # The original callback frame remains on the primary stack.
        return invoke_preserving_context(machine,dict(request,stack=0x2000e000,stop=stop))
    def resolve(handle):return call(dict(entry=0x6f054530,receiver=handle[0],edx=handle[1]))
    assert resolve(identity)==actor
    assert read(0x6fd6860c)==[registry]
    missing=spec.get('missing_spatial_registry',False)
    if missing:write(0x6fd6860c,0)  # Explicit fixture counterfactual, never native parity.
    if spec.get('complete_before_reuse'):
        call(dict(entry=0x6f16d4e0,receiver=group,arguments=[members+44*victim]))
        assert read(actor+0x9c,2)==[0xffffffff]*2
        assert read(actor+0x80,2)==[0,0]
    call(dict(entry=0x6f16eb20,receiver=actor,arguments=[0]))
    assert read(path_pool+0x14,3)==[path-4,2,before_path_pool[2]]
    assert read(path-4)==[before_path_pool[0]]
    released=dict(registry_live=read(registry+0x48)[0],mover_pool=read(pool+0x14,3),
                  spatial_pool=read(owner+0x5d8+0x14,3),old_handle_results=[resolve(h) for h in old_handles],
                  spatial_refs=[read(obj+0x3c)[0] for obj in objects],
                  spatial_slots_live=[read(read(registry+0xc)[0]+h[0]*8)[0]==0xfffffffe for h in old_handles[2:]])
    assert released['old_handle_results']==[0]*4
    assert released['registry_live']==before['registry_live']-(2 if missing else 4),(before,released,old_handles)
    assert released['spatial_slots_live']==[missing,missing]
    assert read(actor+0x14,2)==[0xffffffff]*2
    assert released['mover_pool']==[actor-4,before['mover_pool'][1]-1,before['mover_pool'][2]]
    call(dict(entry=0x6f14ee90,receiver=inputs,arguments=[1]))
    new_actor=read(inputs)[0];new_identity=read(new_actor+0x14,2)
    after=dict(registry_live=read(registry+0x48)[0],mover_pool=read(pool+0x14,3),
               spatial_pool=read(owner+0x5d8+0x14,3),generation_next=read(registry+0x50)[0],
               new_identity=new_identity,old_handle_results=[resolve(h) for h in old_handles])
    assert new_actor==actor and new_identity[0]==identity[0] and new_identity[1]!=identity[1]
    assert resolve(identity)==0 and resolve(new_identity)==new_actor
    assert after['old_handle_results']==[0]*4
    assert after['registry_live']==before['registry_live']+(1 if missing else -1)  # Owned path was released, not recreated.
    assert after['mover_pool']==[before['mover_pool'][0],before['mover_pool'][1],before['mover_pool'][2]+1]
    assert read(new_actor+0x9c,2)==[0xffffffff]*2 and read(new_actor+0xa8)==[0]
    assert read(path_pool+0x14,3)==[path-4,2,before_path_pool[2]]
    machine.emu_start(machine.reg_read(UC_X86_REG_EIP),stop,count=2000000)
    machine.hook_del(hook)
    assert machine.reg_read(UC_X86_REG_EIP)==stop and machine.reg_read(UC_X86_REG_ESP)==stack+4
    survivor=1-victim
    assert read(group+0x38)==[1] and machine.reg_read(UC_X86_REG_EAX)==1
    survivor_row=read(members,11)
    assert survivor_row==before_rows[survivor]
    expected_roles=[n for n in (1,0) if n>=trigger or n!=victim]
    assert [c['role'] for c in callbacks]==expected_roles
    for callback in callbacks:
        expected=new_identity if callback['role']==victim and victim<=trigger else before_rows[callback['role']][:2]
        assert callback['identity']==expected,(spec,callback,expected)
    # A subsequent complete prepass only dispatches the original survivor.
    later=[]
    def observe(uc,address,length,data):later.append(uc.reg_read(UC_X86_REG_ECX))
    hook=machine.hook_add(UC_HOOK_CODE,observe,begin=0x6f16fa00,end=0x6f16fa00)
    run(0x6f16bc10,group);machine.hook_del(hook)
    assert later==[movers[survivor]] and read(members,11)==survivor_row
    for row in before_rows:row[5]='mover'+str(movers.index(row[5]))
    survivor_row[5]='mover'+str(survivor)
    # Only scalar counters and explicit identity tokens survive normalization.
    if spec.get('complete_before_reuse'):
        before['path_pool']=before_path_pool
        released['path_pool']=[path-4,2,before_path_pool[2]]
        after['path_pool']=read(path_pool+0x14,3)
    for phase in (before,released,after):
        phase['mover_pool'][0]=bool(phase['mover_pool'][0])
        phase['spatial_pool'][0]=bool(phase['spatial_pool'][0])
        if 'path_pool' in phase:phase['path_pool'][0]=bool(phase['path_pool'][0])
    return dict(inputs=spec,before=before,released=released,after=after,old_handles=old_handles,
                before_rows=before_rows,surviving_row=survivor_row,callbacks=callbacks,
                later_callback_order=[survivor],reused_same_storage=True,old_generation_rejected=True)


def finish_member(machine, context, run):
    """Complete one original member at slot54 entry, then resume and prune."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
    def read(address,count=1):return list(struct.unpack('<%dI'%count,machine.mem_read(address,4*count)))
    def write(address,*words):machine.mem_write(address,struct.pack('<%dI'%len(words),*(w & 0xffffffff for w in words)))
    group,stack,stop,movers,spec=(context[k] for k in ('group','stack','stop','movers','spec'))
    victim,trigger=spec['victim'],spec['trigger']
    members=read(group+0x28)[0]
    before=[read(members+n*44,11) for n in range(2)]
    pending=True;callbacks=[]
    def boundary(uc,address,length,data):
        nonlocal pending
        actor=uc.reg_read(UC_X86_REG_ECX)
        if pending and actor==movers[trigger]:pending=False;uc.emu_stop()
        else:callbacks.append(movers.index(actor))
    hook=machine.hook_add(UC_HOOK_CODE,boundary,begin=0x6f16fa00,end=0x6f16fa00)
    write(stack,stop);machine.reg_write(UC_X86_REG_ESP,stack);machine.reg_write(UC_X86_REG_ECX,group)
    machine.emu_start(0x6f16bc10,stop,count=2000000)
    assert not pending and machine.reg_read(UC_X86_REG_EIP)==0x6f16fa00
    invoke_preserving_context(machine,dict(entry=0x6f16d4e0,receiver=group,
        arguments=[members+44*victim],stack=0x2000e000,stop=stop))
    assert read(movers[victim]+0x9c,2)==[0xffffffff]*2
    assert read(movers[victim]+0x80,2)==[0,0]
    machine.emu_start(machine.reg_read(UC_X86_REG_EIP),stop,count=2000000);machine.hook_del(hook)
    assert machine.reg_read(UC_X86_REG_EIP)==stop and machine.reg_read(UC_X86_REG_ESP)==stack+4
    assert read(group+0x38)==[1] and read(members,11)==before[1-victim]
    assert callbacks==[n for n in (1,0) if n>=trigger or n!=victim],(spec,callbacks)
    after=read(members,11)
    for row in before+[after]:row[5]='mover'+str(movers.index(row[5]))
    return dict(inputs=spec,before_rows=before,surviving_row=after,callback_order=callbacks,
                completed_member_detached=True,retained_mover_identity=read(movers[victim]+0x14,2),
                retained_mover_path_identity=read(read(movers[victim]+0xa8)[0]+0x14,2))
