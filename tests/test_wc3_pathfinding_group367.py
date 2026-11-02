"""Reject incomplete or altered original Sleep-owner evidence."""
import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_group367 as v

class Sleep367EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text())
        cls.experiments=v.validate(cls.spec)

    def reject(self,change):
        e=copy.deepcopy(self.experiments)
        change(e)
        with self.assertRaises(ValueError):v.runtime(e,self.spec)

    def test_complete_original_repeats_and_controls(self):
        self.assertEqual(v.runtime(self.experiments,self.spec),dict(captures=15,scenes=26,public_markers=332,native_events=622))

    def test_control_must_be_uninstrumented(self):
        self.reject(lambda e:e[1]['cases'][2]['rows'].insert(1,{'event':'trace-end'}))

    def test_owned_process_is_required(self):
        self.reject(lambda e:e[0]['cases'][0]['rows'][0].update(owned=False))

    def test_original_map_identity_is_required(self):
        self.reject(lambda e:e[1]['cases'][0]['rows'][0]['source_sha256'].update(map='0'*64))

    def test_preload_footer_is_required(self):
        self.reject(lambda e:e[1]['cases'][0]['rows'].pop())

    def test_public_control_cannot_diverge(self):
        self.reject(lambda e:e[1]['cases'][2].update(preload=e[1]['cases'][2]['preload'].replace('sleep=1','sleep=0',1)))

    def test_owner_initialization_cannot_be_dropped(self):
        def change(e):
            rows=e[1]['cases'][0]['rows']
            rows.pop(next(i for i,r in enumerate(rows)if r.get('event')=='sleep-pause-initialize'))
        self.reject(change)

    def test_sleep_cannot_acquire_scripted_disable_counter(self):
        def change(e):
            row=next(r for r in e[1]['cases'][0]['rows']if r.get('event')=='sleep-apply-end')
            row['after']['execution']=1
        self.reject(change)

    def test_native_inverse_order_is_preserved(self):
        def change(e):
            rows=e[1]['cases'][0]['rows']
            i=next(i for i,r in enumerate(rows)if r.get('event')=='sleep-pause-remove')
            rows[i],rows[i+1]=rows[i+1],rows[i]
        self.reject(change)

    def test_stock_attachment_positive_control_is_required(self):
        rows=copy.deepcopy(self.experiments[0]['cases'][0]['rows'])
        next(r for r in rows if r.get('event')=='marker'and 'label=add-after 'in r['value']and 'scene=6 'in r['value'])['value']='G367 scene=6 label=add-after result=0 level=0'
        with self.assertRaises(ValueError):v.semantic(rows,0)

    def test_damage_guard_is_semantic(self):
        rows=copy.deepcopy(self.experiments[1]['cases'][0]['rows'])
        row=next(r for r in rows if r.get('event')=='marker'and 'label=damage-after 'in r['value'])
        row['value']=row['value'].replace('life=420.0000','life=410.0000')
        with self.assertRaises(ValueError):v.semantic(rows,1)

    def test_delayed_wake_retains_public_move(self):
        rows=copy.deepcopy(self.experiments[1]['cases'][0]['rows'])
        row=next(r for r in rows if r.get('event')=='marker'and 'label=move-late-before 'in r['value']and 'scene=1 'in r['value'])
        row['value']=row['value'].replace('order=851986','order=851973')
        with self.assertRaises(ValueError):v.semantic(rows,1)

    def typed_rows(self):
        return copy.deepcopy(self.experiments[2]['cases'][0]['rows'])

    def test_witnessed_heap_relocation_preserves_canonical_identity(self):
        rows=self.typed_rows();expected=v.projection(rows)
        for row in rows:
            if row.get('event')=='sleep-pause-source':row['address']=hex(int(row['address'],16)+0x10000)
            if row.get('event')=='suspension-push'and row['parameters'][0]:
                row['parameters'][0]+=0x10000
                row['association']['address']=hex(int(row['association']['address'],16)+0x10000)
        self.assertEqual(v.projection(rows),expected)

    def test_raw_argument_must_match_witness_address(self):
        rows=self.typed_rows()
        next(r for r in rows if r.get('event')=='suspension-push'and r['parameters'][0])['parameters'][0]+=4
        with self.assertRaises(ValueError):v.projection(rows)

    def test_caster_generation_cannot_be_silently_normalized(self):
        rows=self.typed_rows();expected=v.projection(rows)
        for row in rows:
            if row.get('event')=='sleep-pause-source':row['candidate']['generation']+=1
            if row.get('event')=='suspension-push'and row['parameters'][0]:row['association']['snapshot']['generation']+=1
        self.assertNotEqual(v.projection(rows),expected)

    def test_mismatched_canonical_association_is_rejected(self):
        rows=self.typed_rows()
        next(r for r in rows if r.get('event')=='suspension-push'and r['parameters'][0])['association']['snapshot']['slot']+=1
        with self.assertRaises(ValueError):v.projection(rows)

    def test_missing_caster_witness_is_rejected(self):
        rows=self.typed_rows()
        rows.pop(next(i for i,r in enumerate(rows)if r.get('event')=='sleep-pause-source'))
        with self.assertRaises(ValueError):v.projection(rows)

    def test_null_push_cannot_claim_a_caster(self):
        rows=self.typed_rows()
        next(r for r in rows if r.get('event')=='suspension-push'and not r['parameters'][0])['association']={'address':'0x0'}
        with self.assertRaises(ValueError):v.projection(rows)

    def test_actual_sleep_duration_is_preserved(self):
        rows=self.typed_rows()
        next(r for r in rows if r.get('event')=='sleep-main-duration')['word']=0x41b00000
        with self.assertRaises(ValueError):v.nested_semantic(rows)

    def test_authored_aliases_keep_concrete_pause_owner(self):
        rows=copy.deepcopy(self.experiments[3]['cases'][0]['rows'])
        next(r for r in rows if r.get('event')=='pause-initialize')['vtable']='adcb60'
        with self.assertRaises(ValueError):v.semantic(rows,3)

    def test_authored_aliases_require_sleep_main_owner(self):
        rows=copy.deepcopy(self.experiments[3]['cases'][0]['rows'])
        rows.pop(next(i for i,r in enumerate(rows)if r.get('event')=='sleep-initialize'))
        with self.assertRaises(ValueError):v.semantic(rows,3)

    def test_unexpected_stun_factory_is_visible(self):
        rows=copy.deepcopy(self.experiments[3]['cases'][0]['rows'])
        rows.append({'event':'stun-factory','scene':0})
        with self.assertRaises(ValueError):v.semantic(rows,3)

    def test_original_embedded_clock_minimum_is_required(self):
        spec=copy.deepcopy(self.spec);spec['data_guards']['6fcd539c']='00000000'
        with self.assertRaises(ValueError):v.validate(spec)

    def test_paused_refresh_expiry_is_semantic(self):
        rows=copy.deepcopy(self.experiments[4]['cases'][0]['rows'])
        row=next(r for r in rows if r.get('event')=='marker'and 'tick=355 scene=3 'in r['value'])
        row['value']=row['value'].replace('sleep=0','sleep=1')
        with self.assertRaises(ValueError):v.refresh_semantic(rows)

    def test_paused_refresh_requires_original_owner(self):
        rows=copy.deepcopy(self.experiments[4]['cases'][0]['rows'])
        row=copy.deepcopy(next(r for r in rows if r.get('event')=='sleep-main-duration'))
        rows.append(row)
        with self.assertRaises(ValueError):v.refresh_semantic(rows)

    def test_ignored_authored_initial_modifier_is_not_claimed_as_duration(self):
        rows=copy.deepcopy(self.experiments[4]['cases'][0]['rows'])
        next(r for r in rows if r.get('event')=='sleep-pause-source')['duration']=0x3f000000
        with self.assertRaises(ValueError):v.refresh_semantic(rows)

if __name__=='__main__':unittest.main()
