import copy,gzip,json,unittest
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import coupling_producer_evidence as v

class CouplingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
        cls.accepted=v.unpack(cls.bundle['accepted'],'accepted')
        cls.accepted.update(freeze=cls.bundle['accepted']['freeze'],w3a=v.literal(cls.bundle['accepted']['w3a']))
    def test_complete(self):self.assertEqual(v.audit(),dict(observations=2,controls=1,public_markers=999,rejected_missing_ability_markers=999,engine_parity=False,remaining_owner='TARGET-01.2'))
    def test_all_strict_mutation_controls(self):self.assertEqual(len(v.mutation_checks(self.accepted)),12)
    def change(self,event,key,value):
        d=copy.deepcopy(self.accepted);next(r for r in d['captures'][0]['rows'] if r['event']==event)[key]=value;return d
    def test_opposite_target(self):
        with self.assertRaisesRegex(ValueError,'reciprocal task target'):v.audit_accepted(self.change('target-task','target','0x0'))
    def test_native_zero(self):
        with self.assertRaisesRegex(ValueError,'range/caller'):v.audit_accepted(self.change('target-task','range',v.RANGE))
    def test_native_caller(self):
        with self.assertRaisesRegex(ValueError,'range/caller'):v.audit_accepted(self.change('target-task','caller',0))
    def test_opposite_generation(self):
        with self.assertRaisesRegex(ValueError,'reciprocal incarnation'):v.audit_accepted(self.change('couple-ready','partner',[0,0]))
    def test_authored_completion_range(self):
        with self.assertRaisesRegex(ValueError,'completion333'):v.audit_accepted(self.change('couple-ready','range',0))
    def test_reciprocal_internal_order(self):
        d=copy.deepcopy(self.accepted);[r for r in d['captures'][0]['rows'] if r['event']=='couple-producer-enter'][1]['order']=852143
        with self.assertRaisesRegex(ValueError,'reciprocal producers'):v.audit_accepted(d)
    def test_distinct_ready_owners(self):
        d=copy.deepcopy(self.accepted);r=[r for r in d['captures'][0]['rows'] if r['event']=='couple-ready'];r[1]['actor']=r[0]['actor']
        with self.assertRaisesRegex(ValueError,'distinct ready'):v.audit_accepted(d)
    def test_source_literal_integrity(self):
        d=copy.deepcopy(self.bundle);d['accepted']['sources']['itemdrop_probe.j']['raw_sha256']='0'*64
        with self.assertRaisesRegex(ValueError,'literal digest'):v.audit(d)
    def test_rejection_cannot_replace_accepted(self):
        d=copy.deepcopy(self.bundle);d['accepted']=d['rejected_missing_abilities']
        with self.assertRaisesRegex(ValueError,'pinned map'):v.audit(d)
    def test_no_engine_parity(self):
        d=copy.deepcopy(self.bundle);d['engine_parity']=True
        with self.assertRaisesRegex(ValueError,'scope'):v.audit(d)
    def test_rejected_control_hook(self):
        d=v.unpack(self.bundle['rejected_missing_abilities'],'rejected_missing_abilities');d['captures'][2]['rows'].insert(1,dict(event='velocity'))
        with self.assertRaisesRegex(ValueError,'control hooked'):v.audit_rejected(d)
    def test_preserve_inherited_task_label(self):
        d=copy.deepcopy(self.accepted);d['captures'][0]['rows'][0]['task']='TARGET-01.2-coupling'
        with self.assertRaisesRegex(ValueError,'inherited label'):v.audit_accepted(d)


    def test_fixed_compressed_archive_digest(self):
        from unittest.mock import Mock,patch
        with patch.object(v,'BUNDLE',Mock(read_bytes=lambda:b'altered')):
            with self.assertRaisesRegex(ValueError,'compressed bundle digest'):v.audit()

if __name__=='__main__':unittest.main()
