"""Frozen original callback mutations retain ordering and complete member words."""
import hashlib
import itertools
import json
import struct
import sys
from types import SimpleNamespace
from pathlib import Path
import unittest
from unittest.mock import patch

FIXTURES=Path(__file__).resolve().parents[1]/'tools/ghidra/fixtures'
sys.path.insert(0,str(FIXTURES.parent))
from wc3_pathing_callbacks import invoke_preserving_context


class CallbackMachine:
    """Minimal VM double for the orchestration's context/error contract."""
    def __init__(self,failure=None):
        self.registers={name:100+n for n,name in enumerate(('EAX','EBX','ESI','EDI','EBP','ESP','ECX','EDX','EIP'))}
        self.memory={0:bytes(4)};self.failure=failure
    def reg_read(self,register):return self.registers[register]
    def reg_write(self,register,value):self.registers[register]=value
    def mem_read(self,address,size):return self.memory.get(address,bytes(size))[:size]
    def mem_write(self,address,raw):self.memory[address]=bytes(raw)
    def context_save(self):return dict(self.registers)
    def context_restore(self,saved):self.registers=dict(saved)
    def emu_start(self,entry,stop,**options):
        words=struct.unpack('<3I',self.memory[self.registers['ESP']])
        assert words== (stop,11,12) and options==dict(count=2000000)
        self.memory[0x1000]=b'request effect'
        self.registers.update(EAX=77,EIP=stop,ESP=self.registers['ESP']+12)
        if self.failure=='budget':self.registers['EIP']=entry
        elif self.failure=='stack':self.registers['ESP']-=4
        elif self.failure=='register':self.registers['EDI']+=1
        elif self.failure=='exception':self.memory[0]=struct.pack('<I',1)


class CallbackFixtures(unittest.TestCase):
    def test_survivor_reorder_runs_actual_refresh_and_reclaims_both_groups(self):
        for suffix,ticks in (('',[98,55,98,55]),('-wall',[97,61,97,61])):
            fixture=json.loads((FIXTURES/('retail-survivor-retarget'+suffix+'-1.27.json')).read_text())
            canonical=json.dumps(fixture['cases'],sort_keys=True,separators=(',',':')).encode()
            self.assertEqual(hashlib.sha256(canonical).hexdigest(),fixture['cases_sha256'])
            self.assertEqual([c['arrival_tick'] for c in fixture['cases']],ticks)
            self.assertEqual({(c['callback_finish']['inputs']['trigger'],c['callback_finish']['inputs']['victim'])
                for c in fixture['cases']},set(itertools.product(range(2),repeat=2)))
            for case in fixture['cases']:
                replacement=case['survivor_reorder'];survivor=replacement['survivor']
                self.assertNotEqual(replacement['new_group_identity'],replacement['old_group_identity'])
                self.assertEqual(replacement['target_bits'],[0x43c00000,0x43e00000])
                self.assertEqual(replacement['new_group_target'],[0x41400000,0x41600000])
                self.assertEqual(replacement['unit_refs_before'],replacement['unit_refs_after'])
                events=[e for e in case['member_lifecycle'] if e['group_identity']==replacement['new_group_identity']]
                sequence=['0x6f16ce10','0x6f1697a0','0x6f16d990','0x6f16a5b0']
                self.assertEqual([e['entry'] for e in events if e['entry'] in sequence][:4],sequence)
                layouts=[e for e in events if e['entry']=='0x6f16a5b0']
                self.assertEqual(len(layouts),1);self.assertEqual(layouts[0]['count'],1)
                refreshed=[s for s in case['normalized_states'] if s['group']['identity']==replacement['new_group_identity']]
                for state in refreshed:
                    for row in state['group']['members']:
                        self.assertEqual([w & 0x7fffffff for w in row[3:5]],[0,0])
                        if row[:2]!=[0xffffffff]*2:
                            expected=[0,0] if state['phase']=='survivor_reorder' else replacement['new_group_target']
                            self.assertEqual(row[6:8],expected)
                arrivals=case['arrivals'] if survivor==0 else case['second_arrivals']
                self.assertEqual(arrivals[0]['target_bits'],replacement['target_bits'])
                self.assertEqual(arrivals[0]['user_head'],replacement['new_order_identity'])
                destroys=[e for e in case['member_lifecycle'] if e['entry']=='0x6f1699c0']
                self.assertEqual(len(destroys),2);self.assertTrue(all(e['count']==0 for e in destroys))
                cleanup=case['callback_finish']['cleanup']
                self.assertEqual(cleanup['path_pool'],[1,6]);self.assertEqual(cleanup['group_live'],0)

    def test_completed_member_reuse_retains_full_survivor_journey(self):
        for suffix in ('','-wall'):
            fixture=json.loads((FIXTURES/('retail-completed-member-reuse'+suffix+'-1.27.json')).read_text())
            control=json.loads((FIXTURES/('retail-callback-finish'+suffix+'-1.27.json')).read_text())
            canonical=json.dumps(fixture['cases'],sort_keys=True,separators=(',',':')).encode()
            self.assertEqual(hashlib.sha256(canonical).hexdigest(),fixture['cases_sha256'])
            self.assertEqual(len(fixture['cases']),4)
            self.assertEqual({(c['callback_finish']['inputs']['trigger'],c['callback_finish']['inputs']['victim'])
                for c in fixture['cases']},set(itertools.product(range(2),repeat=2)))
            for case,unchanged in zip(fixture['cases'],control['cases']):
                mutation=case['callback_finish'];victim=mutation['inputs']['victim'];survivor=1-victim
                self.assertTrue(mutation['inputs']['complete_before_reuse'])
                old,new=mutation['old_handles'][0],mutation['after']['new_identity']
                self.assertEqual(old[0],new[0]);self.assertNotEqual(old[1],new[1])
                self.assertEqual(mutation['after']['old_handle_results'],[0]*4)
                self.assertEqual(mutation['surviving_row'],mutation['before_rows'][survivor])
                self.assertEqual([mutation[p]['path_pool'][1] for p in ('before','released','after')],[3,2,2])
                allocations=mutation['before']['path_pool'][2]
                self.assertEqual(mutation['released']['path_pool'][2],allocations)
                self.assertEqual(mutation['after']['path_pool'][2],allocations)
                cleanup=mutation['cleanup']
                self.assertEqual(cleanup['path_pool'],[1,allocations])
                self.assertEqual(cleanup['group_live'],0);self.assertEqual(cleanup['unit_refs'],[4,4])
                self.assertIsNone(cleanup['mover_paths'][victim])
                self.assertIsNotNone(cleanup['mover_paths'][survivor])
                self.assertTrue(cleanup['user_queues_empty'] and cleanup['internal_tasks_empty'] and cleanup['owner_lists_empty'])
                self.assertEqual(case['arrival_tick'],unchanged['arrival_tick'])
                # Reclaimed actor state changes, but the original survivor's
                # complete raw motion remains equal to completion without reuse.
                keys=('position','velocity') if survivor==0 else ('second_position','second_velocity')
                self.assertEqual([[t[k] for k in keys] for t in case['trajectory']],
                                 [[t[k] for k in keys] for t in unchanged['trajectory']])

    def test_original_call_restores_cpu_and_retains_memory_even_on_failed_validation(self):
        registers=SimpleNamespace(**{'UC_X86_REG_'+name:name for name in
                  ('EAX','EBX','ESI','EDI','EBP','ESP','ECX','EDX','EIP')})
        with patch.dict(sys.modules,{'unicorn.x86_const':registers}):
            for failure in (None,'budget','stack','register','exception'):
                machine=CallbackMachine(failure);saved=dict(machine.registers)
                call=dict(entry=0x1234,receiver=0x5678,arguments=[11,12],edx=99,stack=0x8000,stop=0x9000)
                if failure:
                    with self.assertRaises(AssertionError):invoke_preserving_context(machine,call)
                else:self.assertEqual(invoke_preserving_context(machine,call),77)
                self.assertEqual(machine.registers,saved)
                self.assertEqual(machine.memory[0x1000],b'request effect')

    def test_completed_member_survivor_and_empty_group_teardown(self):
        for suffix,ticks in (('',{0:13,1:7}),('-wall',{0:42,1:19})):
            fixture=json.loads((FIXTURES/('retail-callback-finish'+suffix+'-1.27.json')).read_text())
            canonical=json.dumps(fixture['cases'],sort_keys=True,separators=(',',':')).encode()
            self.assertEqual(hashlib.sha256(canonical).hexdigest(),fixture['cases_sha256'])
            self.assertEqual(len(fixture['cases']),4)
            self.assertEqual({(c['callback_finish']['inputs']['trigger'],c['callback_finish']['inputs']['victim'])
                for c in fixture['cases']},set(itertools.product(range(2),repeat=2)))
            for case in fixture['cases']:
                mutation=case['callback_finish'];victim=mutation['inputs']['victim'];survivor=1-victim
                self.assertEqual(mutation['surviving_row'],mutation['before_rows'][survivor])
                self.assertEqual(case['arrival_tick'],ticks[victim])
                events=case['member_lifecycle']
                prepares=[e for e in events if e['entry']=='0x6f16bc10']
                destroys=[e for e in events if e['entry']=='0x6f1699c0']
                self.assertEqual(prepares[-1]['identities'],[[0xffffffff]*2])
                self.assertEqual(len(destroys),1);self.assertEqual(destroys[0]['count'],0)
                self.assertEqual(destroys[0]['mover_paths'],prepares[-1]['mover_paths'])
                self.assertEqual(destroys[0]['mover_identities'],prepares[-1]['mover_identities'])
                # Removal alone is not a formation refresh. The original layout
                # occurs once for the pair; the survivor keeps its reserved slot.
                layouts=[e for e in events if e['entry']=='0x6f16a5b0']
                self.assertEqual(len(layouts),1);self.assertEqual(layouts[0]['count'],2)
                for state in case['normalized_states']:
                    rows=state['group']['members']
                    if len(rows)==1 and rows[0][:2]==mutation['surviving_row'][:2]:
                        self.assertEqual(rows[0][3:5],mutation['surviving_row'][3:5])
                        self.assertEqual(rows[0][6:8],mutation['surviving_row'][6:8])
                caps=case['speed_caps'];self.assertEqual([c['cap'] for c in caps[:2]],[0x40800000]*2)
                expected=0x41000000 if survivor==0 else 0x40800000
                self.assertTrue(any(c['cap']==expected for c in caps[2:]))
                self.assertTrue(case['user_order_reclaimed'] and case['shared_pair_completed'])
                self.assertFalse(case['normalized_states'][-1]['group']['active'])

    def test_reused_slot_has_new_generation_and_exact_survivor(self):
        for name in ('retail-callback-reuse-1.27.json','retail-callback-reuse-wall-1.27.json'):
            fixture=json.loads((FIXTURES/name).read_text())
            self.assertFalse(fixture['counterfactual_missing_spatial_registry'])
            self.assertEqual(len(fixture['cases']),4)
            self.assertEqual({(c['inputs']['trigger'],c['inputs']['victim']) for c in fixture['cases']},
                             set(itertools.product(range(2),repeat=2)))
            for case in fixture['cases']:
                old,new=case['old_handles'][0],case['after']['new_identity']
                self.assertEqual(old[0],new[0]);self.assertNotEqual(old[1],new[1])
                self.assertTrue(case['reused_same_storage'] and case['old_generation_rejected'])
                before=case['before']['registry_live']
                self.assertEqual(case['released']['registry_live'],before-4)
                self.assertEqual(case['after']['registry_live'],before-1)
                self.assertEqual(case['released']['spatial_slots_live'],[False,False])
                self.assertEqual(case['released']['old_handle_results'],[0]*4)
                self.assertEqual(case['after']['old_handle_results'],[0]*4)
                victim=case['inputs']['victim'];survivor=1-victim
                self.assertEqual(case['surviving_row'],case['before_rows'][survivor])
                self.assertEqual(case['later_callback_order'],[survivor])
                self.assertEqual(case['released']['spatial_refs'],[2,2])
                # Retired objects retain references; new activation uses spare
                # pool backing and does not pretend those objects were freed.
                self.assertEqual(case['after']['spatial_pool'][1],case['before']['spatial_pool'][1]+2)

    def test_missing_alias_rejects_old_handles_but_leaves_stale_slots(self):
        fixture=json.loads((FIXTURES/'retail-callback-reuse-missing-alias-1.27.json').read_text())
        self.assertTrue(fixture['counterfactual_missing_spatial_registry'])
        for case in fixture['cases']:
            self.assertEqual(case['released']['old_handle_results'],[0]*4)
            self.assertEqual(case['released']['spatial_slots_live'],[True,True])
            before=case['before']['registry_live']
            self.assertEqual(case['released']['registry_live'],before-2)
            self.assertEqual(case['after']['registry_live'],before+1)

    def test_every_callback_position_and_removal_subset_is_recorded(self):
        fixture=json.loads((FIXTURES/'retail-callback-mutations-1.27.json').read_text())
        cases=fixture['cases']
        expected={(count,trigger,removed,action) for count in range(1,4)
                  for trigger,removed,action in itertools.product(range(count),range(1<<count),
                                                                  ['unbind_member','detach_mover'])}
        observed={(c['count'],c['trigger'],c['removed'],c['action']) for c in cases}
        self.assertEqual(observed,expected)
        self.assertEqual(len(cases),len(expected))
        digest=hashlib.sha256(json.dumps(cases,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,fixture['cases_sha256'])
        self.assertIn('handle reclaim/reuse excluded',fixture['scope'])

    def test_post_callback_resolution_preserves_all_surviving_words(self):
        cases=json.loads((FIXTURES/'retail-callback-mutations-1.27.json').read_text())['cases']
        for case in cases:
            count,trigger,removed=(case[k] for k in ('count','trigger','removed'))
            # Higher rows already ran; lower rows must re-resolve after mutation.
            callbacks=list(reversed(range(trigger,count)))
            callbacks.extend(n for n in reversed(range(trigger)) if not removed & (1<<n))
            self.assertEqual(case['callback_order'],callbacks)
            survivors=list(range(count))
            for index in reversed(range(count)):
                if removed & (1<<index):
                    survivors[index]=survivors[-1]
                    survivors.pop()
            self.assertEqual(case['later_callback_order'],list(reversed(survivors)))
            words=[]
            for actor in survivors:
                row=[actor,100+actor]+[0x24680000+16*actor+k for k in range(2,11)]
                row[5]='mover'+str(actor)
                words.append(row)
            self.assertEqual(case['surviving_rows'],words)
        swapped=next(c for c in cases if (c['count'],c['trigger'],c['removed'],c['action'])==
                     (3,2,1,'detach_mover'))
        self.assertEqual([r[0] for r in swapped['surviving_rows']],[2,1])
        self.assertEqual(swapped['callback_order'],[2,1])
        self.assertEqual(swapped['later_callback_order'],[1,2])

    def test_persisted_member_layout_decodes_original_pair_requests(self):
        schema=json.loads((FIXTURES/'retail-pathfinding-types-1.27.json').read_text())
        types={t['name']:t for t in schema['layouts']}
        member=types['WC3PathMember']
        offsets={f['name']:f['offset'] for f in member['fields']}
        self.assertEqual(member['length'],44)
        self.assertNotIn(8,offsets.values())  # Unrecovered word stays undefined.
        pair=json.loads((FIXTURES/'retail-shared-pair-1.27.json').read_text())
        rows=pair['output']['normalized_states'][0]['group']['members']
        requests=[]
        for row in rows:
            requests.append({name:row[offsets[name]//4] for name in ('resolved','speed','heading','flags')})
        self.assertEqual(requests,[dict(resolved='mover',speed=0x41000000,heading=0xbe7aeac0,flags=0x100000),
                                   dict(resolved='second_mover',speed=0x41000000,heading=0xbe7aeac0,flags=0x100000)])
        group={f['name']:f['offset'] for f in types['WC3PathGroupPrefix']['fields']}
        vector={f['name']:f['offset'] for f in types['WC3PathMembersPrefix']['fields']}
        self.assertEqual(group['members']+vector['data'],0x28)
        self.assertEqual(group['members']+vector['count'],0x38)


if __name__=='__main__':unittest.main()
