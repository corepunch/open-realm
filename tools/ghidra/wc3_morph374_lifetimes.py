"""Canonicalize only guarded Unit128/12c handle ownership lifetimes."""
import copy
from verify_wc3_pathing_morph372 import PROJECTION_CONTRACT as PARENT_CONTRACT
CONTRACT=dict(literal_unit_offsets=[0x118,0x11c,0x120,0x124],pointer_unit_offsets=[0x128,0x12c],
    pointer_mapping='global concurrent-holder bijection; retire only observed last-holder literalzero; new ownership gets new monotonic token',
    proof='Guarded3e3c00/3e3bd0 cancel virtual8, release pointee4 refcount, destroy atzero and clear ECX-owned handle. '
          '6696e0/694b20/68b780 pass exactUnit128/12c addresses.058120 allocates a new FloatModifier. '
          'Observed literalzero boundaries permit later addressreuse; concurrent aliases retain one token. '
          'All other scalar words, stable identities, clocks, sequence and systemexception rows stay literal.',
    proof_instruction_sites=PARENT_CONTRACT['proof_instruction_sites']+['6f3e3c0b','6f3e3c14','6f3e3c1b','6f3e3c1d',
        '6f3e3bdb','6f3e3be4','6f3e3beb','6f3e3bed'])

def project(rows):
    live={};holders={};serial=0;result=[];ledger=[]
    for raw in rows:
        if ('seq'not in raw and raw.get('event')!='native-exception')or raw['event']=='module':continue
        row=copy.deepcopy(raw)
        for key in('before','after','state'):
            state=row.get(key)
            if not isinstance(state,dict)or 'invisibilityScalar'not in state:continue
            words=state['invisibilityScalar']
            if len(words)!=6 or len(state.get('identity',[]))!=2:raise ValueError('incomplete scalar/holder identity')
            unit=tuple(state['identity']);new={i:words[i]for i in(4,5)};old=holders.get(unit,{4:0,5:0});changed=set()
            for i in(4,5):
                ptr=old[i]
                if ptr not in(0,0xffffffff)and ptr!=new[i]:
                    if new[i]!=0:raise ValueError('pointer replacement without observed literalzero')
                    live[ptr]['holders'].remove((unit,i));changed.add(ptr)
            # Update the complete snapshot before retiring any last holder.
            # An alias remains live while any unit/field still owns it.
            for i in(4,5):
                ptr=new[i]
                if ptr not in(0,0xffffffff):
                    if ptr not in live:
                        serial+=1;live[ptr]=dict(token=serial,holders=set())
                    live[ptr]['holders'].add((unit,i));words[i]=['scalar-owner-lifetime',live[ptr]['token']]
            for ptr in sorted(changed,key=lambda value:live[value]['token']):
                if not live[ptr]['holders']:
                    ledger.append(dict(token=live[ptr]['token'],seq=raw['seq'],tick=raw['tick'],identity=list(unit),pointer=ptr,
                        cleared_offsets=[0x118+i*4 for i in(4,5)if old[i]==ptr and new[i]==0]))
                    del live[ptr]
            holders[unit]=new
        result.append(row)
    if live:raise ValueError('capture ends with unretired observed pointer holders')
    return result,ledger

def projection(rows):return project(rows)[0]
