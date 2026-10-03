#!/usr/bin/env python3
"""Original CAbilityMove speed producer, clamps and concrete AIms aggregation.

Full calls through real ability lists, profile hash lookup and startup
constant initializers; exact independent soft-float reference.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import bits, add, subtract, multiply, MASK


def value(word):
    return struct.unpack('<f',struct.pack('<I',word))[0]


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
    from unicorn.x86_const import (UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,
        UC_X86_REG_EAX,UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI)
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True,type=Path)
    parser.add_argument('--report',required=True,type=Path)
    parser.add_argument('--engine-library',type=Path)
    parser.add_argument('--fixture',type=Path)
    args=parser.parse_args();binary=args.binary.read_bytes()
    engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine: engine.pathing_speed_bonus.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
    bonus_cases=[]
    digest=hashlib.sha256(binary).hexdigest()
    if digest!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('requires retail game.dll 1.27.1.7085')
    pe=struct.unpack_from('<I',binary,60)[0];opt=pe+24
    base,size=[struct.unpack_from('<I',binary,opt+n)[0] for n in [28,56]]
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    uc.mem_write(base,binary[:struct.unpack_from('<I',binary,opt+60)[0]])
    for i in range(struct.unpack_from('<H',binary,pe+6)[0]):
        s=opt+struct.unpack_from('<H',binary,pe+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',binary,s+12)
        if count:uc.mem_write(base+va,binary[offset:offset+count])
    uc.mem_map(0x10000000,0x10000);uc.mem_map(0x20000000,0x10000)
    registry,slots,wrapper,ability,unit,bucket,profile,output=[0x10000000+n for n in [0,0x100,0x200,0x400,0x800,0xc00,0x1000,0x2000]]
    stack,stop=0x20008000,0x30000000
    def write(address,*words):uc.mem_write(address,struct.pack('<'+'I'*len(words),*(w&MASK for w in words)))
    def read(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
    def run(entry,self,*arguments):
        write(stack,stop,*arguments);uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_ECX,self)
        uc.emu_start(entry,stop,count=100000)
        assert uc.reg_read(UC_X86_REG_EIP)==stop
        assert uc.reg_read(UC_X86_REG_ESP)==stack+4*(len(arguments)+1)
    write(0x6fd3c740,bits(-1),0,bits(1))
    startup=[0x6f016290,0x6f0162b0,0x6f0162c0,0x6f0162d0,0x6f016210,0x6f016220,0x6f016230,0x6f016240]
    for entry in startup:run(entry,0)
    stock=[read(0x6fd709b8+n*4) for n in range(8)]
    assert stock==list(map(bits,[1,522,1,522,1,522,1,522]))
    write(0x6fd68610,registry);write(registry+0xc,slots);write(registry+0x1c,1)
    write(slots,-2,wrapper);write(wrapper,0x6fa8099c);write(wrapper+0x14,0,100);write(wrapper+0x54,ability)
    write(ability,0x6fb62794);write(ability+0x24,-1,-1);write(ability+0x30,unit)
    write(unit,0x6fb77eb0);write(unit+0x30,0x68666f6f)
    write(profile+0x14,0x68666f6f);run(0x6f198420,profile+0x14);write(profile,uc.reg_read(UC_X86_REG_EAX))
    write(bucket,0,0,profile);write(0x6fd709f4,bucket);write(0x6fd709fc,0)
    assert read(0x6fb62794+0x184)==0x6f435760,hex(read(0x6fb62794+0x184))
    # Existing class-query cache records for Amov/AIms -> Bply=false. These
    # are fixture data, not replacement callbacks; original hash lookup and
    # query consumers execute. Class registration/cache-miss construction is
    # outside this oracle. Concrete vtables and RTTI establish the classes.
    game=0x10003000
    write(0x6fd3c82c,game)
    class_bucket=game+0x400
    class_rows=[game+0x100,game+0x200]
    write(game+0xc+0x1c,class_bucket);write(game+0xc+0x24,0)
    write(class_bucket,8,0,class_rows[0])
    for i,(row,rawcode) in enumerate(zip(class_rows,[0x416d6f76,0x41496d73])):
        write(output,rawcode);run(0x6f198420,output)
        write(row+4,uc.reg_read(UC_X86_REG_EAX));write(row+0x18,rawcode)
        write(row+0xc,class_rows[i+1] if i+1<len(class_rows) else 0)
        cache_bucket=game+0x500+i*0x100;cache_entry=cache_bucket+0x40
        write(row+0x1c+0x1c,cache_bucket);write(row+0x1c+0x24,0)
        write(cache_bucket,0,0,cache_entry)
        write(output,0x42706c79);run(0x6f198420,output)
        write(cache_entry,uc.reg_read(UC_X86_REG_EAX),0)
        write(cache_entry+0x14,0x42706c79,0)
    bonus_wrappers=[0x10005000,0x10005200]
    bonus_abilities=[0x10006000,0x10006200]
    for index,(host,bonus) in enumerate(zip(bonus_wrappers,bonus_abilities),1):
        write(host,0x6fa8099c);write(host+0x14,index,100+index);write(host+0x54,bonus)
        write(slots+index*8,-2,host)
        write(bonus,0x6fb1f1a0);write(bonus+0x24,-1,-1);write(bonus+0x30,unit)
    write(registry+0x1c,3)
    assert read(0x6fb1f1a0+0x184)==0x6f569830
    def invalid(machine,access,address,size,value,data):
        print(f"Invalid memory {address:#x} at {machine.reg_read(UC_X86_REG_EIP):#x}",flush=True)
        return False
    uc.hook_add(UC_HOOK_MEM_INVALID,invalid)
    trace=[]
    addresses=[0x6f5fc900,0x6f48f410,0x6f435760,0x6f68c190,0x6f48cb80,0x6f68f010,0x6f68ed40,0x6f5fc962,0x6f569830,0x6f04ce00]
    def observe(machine,address,size,data):
        trace.append((address,read(output) if address==0x6f5fc962 else None))
    for address in addresses:uc.hook_add(UC_HOOK_CODE,observe,begin=address,end=address)
    # Observe raw arithmetic through direct5fc900 only: outer5fc890 uses local output.
    preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
    counts={'ordinary':0,'adjacent':0,'status_defaults':0,'profile_missing':0,'attached_move':0,'move_speed_bonus':0}
    no_buff_query_count=0
    def check(category,base_speed,additive,multiplier,pmin,pmax,status=0,config=None,present=True,attached=False,outer=True,bonuses=(),reverse=False):
        nonlocal no_buff_query_count
        constants=stock if config is None else list(map(bits,config))
        write(0x6fd709b8,*constants)
        write(unit+0x5c,status)
        rows=([(ability,0,100)] if attached else []) + [(bonus_abilities[i],i+1,101+i) for i in range(len(bonuses))]
        if reverse:rows.reverse()
        write(unit+0x1dc,*(rows[0][1:] if rows else [-1,-1]))
        for i,(ptr,index,generation) in enumerate(rows):
            write(ptr+0x24,*(rows[i+1][1:] if i+1<len(rows) else [-1,-1]))
        for ptr,bonus in zip(bonus_abilities,bonuses):write(ptr+0x88,bonus)
        write(ability+0x38,base_speed);write(ability+0x70,additive);write(ability+0x78,multiplier);write(ability+0x7c,0)
        write(profile+0x1d8,pmin,pmax);write(bucket+8,profile if present else 0)
        index=2 if status&0x10000 else 0
        lower,upper=constants[index:index+2]
        minword=pmin if present else 0;maxword=pmax if present else 0
        minimum=constants[4+index] if minword==0 else min(max(value(minword),value(lower)),value(upper))
        maximum=constants[5+index] if maxword==0 else min(max(value(maxword),value(lower)),value(upper))
        # Default words bypass clamping; nonzero profile values are clamped first.
        minbits=minimum if minword==0 else bits(minimum)
        maxbits=maximum if maxword==0 else bits(maximum)
        observed_base=max([0,*bonuses],key=value)  # Maximum bonus, not sum.
        raw=multiply(add(additive,observed_base),multiplier)
        expected=bits(min(max(value(raw),value(minbits)),value(maxbits)))
        watched=[(unit,0x300),(ability,0x200),(wrapper,0x100),(registry,0x100),(profile,0x200),(bucket,12),(game,0x800),*[(p,0x100) for p in bonus_abilities]]
        before=[bytes(uc.mem_read(a,n)) for a,n in watched]
        write(output-4,0xdead1234,0xdead5678,0xdeadabcd)
        trace.clear()
        for i,r in enumerate(preserved):uc.reg_write(r,0x12120000+i)
        run(0x6f5fc890 if outer else 0x6f5fc900,ability,output)
        assert read(output)==expected,(category,hex(base_speed),hex(additive),hex(multiplier),hex(pmin),hex(pmax),status,config,hex(read(output)),hex(expected))
        assert uc.reg_read(UC_X86_REG_EAX)==output
        assert [uc.reg_read(r) for r in preserved]==[0x12120000+i for i in range(4)]
        assert read(output-4)==0xdead1234 and read(output+4)==0xdeadabcd
        assert before==[bytes(uc.mem_read(a,n)) for a,n in watched]
        for address in [0x6f5fc900,0x6f48f410,0x6f68c190,0x6f48cb80,0x6f68f010,0x6f68ed40]:
            assert sum(a==address for a,_ in trace)==1
        assert sum(a==0x6f435760 for a,_ in trace)==int(attached)
        assert sum(a==0x6f569830 for a,_ in trace)==len(bonuses)
        assert sum(a==0x6f04ce00 for a,_ in trace)==len(rows)
        if not outer:assert [(a,v) for a,v in trace if a==0x6f5fc962]==[(0x6f5fc962,raw)]
        if category=='move_speed_bonus':
            inputs=[additive,*list(bonuses),*([0]*(2-len(bonuses))),multiplier,pmin,pmax,constants[4],constants[5]]
            outputs=[observed_base,raw,read(output)]
            if engine:
                actual=(ctypes.c_uint32*3)()
                engine.pathing_speed_bonus((ctypes.c_uint32*8)(*inputs),actual)
                assert list(actual)==outputs,(inputs,list(actual),outputs)
            bonus_cases.append(dict(input=inputs,output=outputs))
        counts[category]+=1;no_buff_query_count+=1
        return read(output)
    for base_speed,additive,multiplier,pmin,pmax,status in itertools.product(
            [0,270,350],[-100,0,.499,256,1000],[.25,1,1.1,2],[0,1,100],[0,400,522],[0,0x10000]):
        check('ordinary',*map(bits,[base_speed,additive,multiplier,pmin,pmax]),status,outer=False)
    for threshold,delta,outer in itertools.product([1,100,400,522],[-1,0,1],[False,True]):
        check('adjacent',bits(270),bits(threshold)+delta,bits(1),bits(100),bits(400),outer=outer)
    config=[10,500,20,400,30,450,40,350]
    for status,pmin,pmax,speed in itertools.product([0,0x10000,0x10080],[0,1,100,1000],[0,1,200,1000],[0,35,300,1000]):
        check('status_defaults',bits(0),bits(speed),bits(1),bits(pmin),bits(pmax),status,config,attached=False)
    for status,speed,attached in itertools.product([0,0x10000],[0,16,256,1000],[False]):
        check('profile_missing',bits(270),bits(speed),bits(1),bits(100),bits(400),status,config,present=False,attached=attached)
    # Runtime defaults outside global bounds prove zero-profile selection is
    # direct, whereas explicit profile values take the clamp path.
    outside_config=[10,500,20,400,1,700,5,600]
    for status,pmin,pmax,speed in itertools.product([0,0x10000],[0,1],[0,700],[0,2,550,1000]):
        check('status_defaults',bits(270),bits(speed),bits(1),bits(pmin),bits(pmax),status,outside_config)
    positive_witness=check('ordinary',bits(270),bits(256),bits(1),bits(1),bits(522),outer=True)
    assert positive_witness==bits(256)
    for speed,status,outer in itertools.product([0,1,270,522,1000],[0,0x10000],[False,True]):
        check('attached_move',bits(350),bits(speed),bits(1),bits(1),bits(522),status,attached=True,outer=outer)
    for bonuses,speed,multiplier,reverse in itertools.product(
            [(-10,), (0,), (25,), (50,25), (25,50), (50,50), (.499,.5)],
            [0,270,500],[.5,1,1.1],[False,True]):
        check('move_speed_bonus',bits(350),bits(speed),bits(multiplier),bits(1),bits(522),
            attached=True,outer=False,bonuses=tuple(map(bits,bonuses)),reverse=reverse)
    # Complete FloatMini setter -> recompute -> real CUnit bridge -> mover.
    # Resting, ungrouped mover avoids unrelated velocity-clipping occupancy
    # work; the ordinary publication path itself is fully executed.
    uc.mem_map(0,0x1000)
    mover,owner=0x10007000,0x10008000
    write(mover,0x6fa9129c);write(mover+0x14,3,103)
    write(slots+24,-2,mover);write(registry+0x1c,4)
    write(unit+0x164,0x6fac2f10,0,3,103)
    write(unit+0x1ec,ability)
    write(ability+0x6c,0x6fa9de14);write(ability+0x74,0x6fa9de14)
    write(mover+0x9c,-1,-1);write(mover+0x70,bits(.125),4,bits(8),bits(9),0,0)
    write(0x6fd53a48,owner);write(owner+0x54,bits(.5),4,bits(8))
    write(0x6fd6a8b8,0) # No CGameUI instance: original notification returns.
    producer_entries=[0x6f247520,0x6f6005d0,0x6f05c5c0,0x6f5fcd50,0x6f5fc0e0,0x6f058bc0,0x6f16e7c0]
    for address in producer_entries:uc.hook_add(UC_HOOK_CODE,observe,begin=address,end=address)
    uc.ctl_flush_tb()
    epsilon=read(0x6fcd53a0)
    producer_counts={'assign':0,'additive':0,'multiplier':0,'unit_assign':0}
    slow_transitions={'enter':0,'exit':0,'unchanged':0}
    def slow(word):return value(word)<1 and value(subtract(word,bits(1))&0x7fffffff)>=value(epsilon)
    def producer(kind,old_speed,old_multiplier,operand,cap=522):
        write(unit+0x1dc,-1,-1);write(unit+0x5c,0)
        write(ability+0x70,old_speed);write(ability+0x78,old_multiplier)
        write(ability+0x7c,0,int(slow(old_multiplier)))
        write(0x6fd709b8,*stock);write(profile+0x1d8,bits(1),bits(522));write(bucket+8,profile)
        write(game+0x80,bits(cap))
        write(mover+0x88,bits(99));write(mover+0xb4,bits(98))
        argument=output+0x20;write(argument,operand)
        next_speed,next_multiplier=old_speed,old_multiplier
        noop=kind=='multiplier' and value(operand)==0
        if kind in ['assign','unit_assign']:next_speed=operand
        elif kind=='additive':
            next_speed=add(old_speed,operand)
            if value(subtract(next_speed,epsilon)&0x7fffffff)<value(epsilon):next_speed=epsilon
        elif not noop:
            next_multiplier=add(old_multiplier,operand)
            if value(subtract(next_multiplier,bits(1))&0x7fffffff)<value(epsilon):next_multiplier=bits(1)
        next_slow=slow(next_multiplier)
        raw=multiply(add(next_speed,0),next_multiplier)
        final_speed=min(max(value(raw),1),522)
        published=bits(min(final_speed,cap))-0x2800000
        mover_before=bytearray(uc.mem_read(mover,0x100))
        ability_before=bytearray(uc.mem_read(ability,0x200))
        snapshots=[bytes(uc.mem_read(a,n)) for a,n in [(unit,0x300),(game,0x800),(owner,0x100),(registry,0x100),(argument,4)]]
        struct.pack_into('<I',ability_before,0x70,next_speed)
        struct.pack_into('<I',ability_before,0x78,next_multiplier)
        struct.pack_into('<I',ability_before,0x80,int(next_slow))
        if not noop:
            struct.pack_into('<I',mover_before,0x88,published)
            struct.pack_into('<I',mover_before,0xb4,published)
        trace.clear();write(0,0x12345678)
        for i,r in enumerate(preserved):uc.reg_write(r,0x12120000+i)
        entries={'assign':0x6f5fee60,'unit_assign':0x6f698af0,'additive':0x6f5fb740,'multiplier':0x6f5fb7c0}
        run(entries[kind],unit if kind=='unit_assign' else ability,argument)
        assert bytes(uc.mem_read(ability,0x200))==bytes(ability_before),(kind,hex(old_speed),hex(old_multiplier),hex(operand),'ability')
        assert bytes(uc.mem_read(mover,0x100))==bytes(mover_before),(kind,hex(old_speed),hex(old_multiplier),hex(operand),'mover')
        assert snapshots==[bytes(uc.mem_read(a,n)) for a,n in [(unit,0x300),(game,0x800),(owner,0x100),(registry,0x100),(argument,4)]]
        assert read(0)==0x12345678
        assert [uc.reg_read(r) for r in preserved]==[0x12120000+i for i in range(4)]
        for address in [0x6f6005d0,0x6f05c5c0]:assert sum(a==address for a,_ in trace)==int(not noop)
        enter=kind=='multiplier' and not slow(old_multiplier) and next_slow
        leave=kind=='multiplier' and slow(old_multiplier) and not next_slow
        assert sum(a==0x6f5fcd50 for a,_ in trace)==int(enter)
        assert sum(a==0x6f5fc0e0 for a,_ in trace)==int(leave)
        assert sum(a==0x6f16e7c0 for a,_ in trace)==int(enter)
        producer_counts[kind]+=1
        if kind=='multiplier':slow_transitions['enter' if enter else 'exit' if leave else 'unchanged']+=1
    for kind,speed,multiplier,cap in itertools.product(['assign','unit_assign'],[0,.499,1,100,270,522,1000],[.5,1,1.1],[100,522]):
        producer(kind,bits(270),bits(multiplier),bits(speed),cap)
    for old,delta in itertools.product([0,.001,1,270],[-270,-1,-.001,0,.001,.499,25]):
        producer('additive',bits(old),bits(1),bits(delta))
    for threshold,adjacent in itertools.product([0,epsilon,bits(.002)],[-1,0,1]):
        if threshold+adjacent>=0:producer('additive',bits(0),bits(1),threshold+adjacent)
    for old,delta in itertools.product([.5,.999,1,1.001,1.5],[-.5,-.001,0,.001,.5]):
        producer('multiplier',bits(270),bits(old),bits(delta))
    for sign,adjacent in itertools.product([0,0x80000000],[-1,0,1]):
        producer('multiplier',bits(270),bits(1),(epsilon+adjacent)|sign)
    report=dict(passed=True,binary_sha256=digest,cases=counts,polymorph_absent_queries=no_buff_query_count,positive_speed_witness=256,
        tested_runtime_configs=[config,outside_config],producer_cases=producer_counts,slow_transitions=slow_transitions,
        producer_contract='Full FloatMini setter247520 -> speed recompute6005d0 -> real CUnit6864d0 -> movement bridge05c5c0 -> cap game80, /32 -> mover88 and b4. Zero multiplier delta returns without publication. Slow entry increments80 and notifies original ungrouped mover path058bc0/16e7c0; exit decrements80. Clock, position and zero velocity unchanged.',
        producer_assertions=['Exact complete ability/mover byte images including slow counter',
            'Unchanged unit/game/owner/registry/argument', 'Publication and slow entry/exit callback counts',
            'Actual FS exception-chain restoration and callee-saved registers'],
        static_producers={
            '5f9f9d..5f9fbe constructor region':'FloatMini objects at6c/74 start values70/78=0, both actual vtablea9de14',
            '5fcd80 initialization':'CAbilityMove vtable308; stores supplied initial-speed pointer into70, writes78=1, zeros7c/80, attaches unit/subscription, forwards speed through unit movement bridge',
            '5fee60 setter':'Assigns70 through original embedded FloatMini virtual0 then6005d0 refresh; callers698af0 and670950',
            '5fb740 additive change':'Retail-add input delta into70, snap within .001 of .001 via embedded setter; then6005d0 refresh; callers52aad0(hero-derived),6417c0/64c090(transform chains),6b6740/6b67a0(unit-field modifiers)',
            '5fb7c0 multiplier change':'Adds input delta into78, snaps within.001 of1, handles crossing slow-status threshold via5fcd50/5fc0e0; then6005d0 refresh',
            '6005d0 publication':'Recompute5fc890, unit vtableb8 movement bridge then bridge18 setter; final shared notification3770c0'},
        contract='base=max(0, ability-list virtual184 speeds); raw=retail_multiply(retail_add(ability70,base),ability78); each nonzero profile bound clamped to status-selected global limits, zero/missing selects default without clamp; apply lower bound then upper bound',
        startup_entries=[hex(a) for a in startup],startup_global_words={hex(0x6fd709b8+n*4):hex(w) for n,w in enumerate(stock)},
        provenance={'base':'48f410 executes empty, move-only, and move+AIms lists. Original CAbilityMove184=435760 returns0; CAbilityMoveSpeedBonus184=569830 reads88. Aggregator takes max(0,bonuses), not sum. Inputs controlled; not stock SLK ingestion',
            'profile':'Controlled hfoo-keyed row resolved by original198420 hash and68f010/68ed40;1d8/1dc bound fields',
            'defaults':'Executed original016290/2b0/2c0/2d0 then016210/220/230/240; additional distinct synthetic runtime values verify status/default selection',
            'polymorph':'Original Bply query executes empty or registered concrete ability lists with preexisting negative Amov/AIms class-cache records; cache-miss construction excluded'},
        assertions=['Exact output bits','Direct producer intermediate raw arithmetic','Original callback counts','ABI/guard words and unchanged unit/ability/profile/registry'],
        exclusions=['Stock profile/SLK ingestion, bonus88 producers, class-registration/cache-miss construction, other buff speed modifiers','Polymorph alternate profile','unit194==5 with20bit8000 branch','unit20bit08000000 special cap','Suppression counter7c nonzero; all tested calls use ordinary production','Nonfinite inputs',
                    'Setter publication with nonzero mover velocity/clipping or attached group',
                    'CGameUI notification work with non-null UI instance'])
    if args.fixture: args.fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=bonus_cases,scope='Complete original Move speed plus attached AIms maximum; controlled DataA values and ordinary limits'),separators=(',',':'))+'\n')
    report['engine_bonus_cases']=len(bonus_cases) if engine else 0
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
