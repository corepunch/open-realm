"""Reject altered original phase requests and preserve prior public evidence."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from verify_wc3_pathing_cast283 import SPEC,BUNDLE,requests,validate_runtime


class Cast283Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(SPEC.read_text())
        cls.bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()))

    def changed(self,event):
        bundle=copy.deepcopy(self.bundle)
        rows=bundle['groups'][0]['captures'][0]['rows']
        # Select an owned row, rather than a constructor/helper occurrence.
        contract=requests(rows)
        scene,fields=next(x for x in contract if x[1]['event']==event)
        return bundle,next(r for r in rows if r['event']==event and r['c']==fields['c']
                           and r.get('phase')==fields.get('phase') and r.get('id')==fields.get('id'))

    def test_repeats_controls_and_prior_public_words(self):
        self.assertEqual(validate_runtime(self.bundle,self.spec),dict(captures=4,controls=2,public_markers=2130))

    def test_clock_bit_is_literal(self):
        bundle,row=self.changed('phase-event');row['clock'][0]^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_delay_bit_is_literal(self):
        bundle,row=self.changed('phase-arm');row['delay']^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_request_id_is_literal(self):
        bundle,row=self.changed('phase-arm');row['id']^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_virtual_dispatch_is_literal(self):
        bundle,row=self.changed('phase-method');row['rva']^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_success_argument_is_literal(self):
        bundle,row=self.changed('phase-method');self.assertEqual(row['slot'],0x39c);row['arg']^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_finish_flags_are_literal(self):
        bundle,row=self.changed('finish-body');row['flags']^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_scalar_dispatch_is_literal(self):
        bundle,row=self.changed('finish-body');row['delays'][1]^=1
        with self.assertRaisesRegex(ValueError,'request words'):validate_runtime(bundle,self.spec)

    def test_missing_row_is_rejected(self):
        bundle,row=self.changed('phase-arm');bundle['groups'][0]['captures'][0]['rows'].remove(row)
        with self.assertRaisesRegex(ValueError,'observer incomplete'):validate_runtime(bundle,self.spec)

    def test_changed_source_is_rejected(self):
        bundle,row=self.changed('phase-arm');bundle['groups'][0]['captures'][0]['rows'][0]['source_sha256']['cast283_observer.js']='0'*64
        with self.assertRaisesRegex(ValueError,'captured source'):validate_runtime(bundle,self.spec)

    def test_control_cannot_have_hooks(self):
        bundle=copy.deepcopy(self.bundle);bundle['groups'][1]['captures'][2]['rows'].insert(-1,dict(event='phase-event'))
        with self.assertRaisesRegex(ValueError,'hooked control'):validate_runtime(bundle,self.spec)

    def test_channel_interruption_preserves_identity_and_clears_live_target(self):
        public=self.spec['groups']['mutations']['public']
        for scene in range(4):
            rows=[r for r in public if f'scene={scene} ' in r and 'label=context ' in r]
            phases=[r.split('phase=')[1].split()[0] for r in rows]
            self.assertEqual(phases,['player-channel','player-endcast','unit-endcast','unit-channel','player-finish','unit-finish'])
            for row in rows[3:]:
                self.assertIn('tx=0.0000 ty=0.0000 target=0',row)
                self.assertIn('caster=CASTER trigger=CASTER',row)

    def test_canonical_spell_prefix_keeps_prior_fields(self):
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        layout=next(x for x in schema['layouts'] if x['name']=='WC3SpellAbilityPrefix')
        fields={x['offset']:x for x in layout['fields']}
        self.assertEqual(layout['length'],0x98)
        self.assertEqual([(o,fields[o]['name']) for o in (0,0x30,0x50,0x54)],
                         [(0,'vtable'),(0x30,'caster'),(0x50,'rank_index'),(0x54,'cached_rank_rows')])
        self.assertEqual([(o,fields[o]['name'],fields[o]['type']) for o in (0x80,0x88,0x90)],
                         [(0x80,'channel_delay','WC3FloatMini'),(0x88,'cast_point','WC3FloatMini'),(0x90,'backswing','WC3FloatMini')])

    def test_broad_class_scope_stays_open(self):
        self.assertIn('ORDER-01.13 remains open',self.spec['scope'])
        self.assertEqual(len(self.spec['groups']['phases']['requests']),317)
        self.assertEqual(len(self.spec['groups']['mutations']['requests']),299)


if __name__=='__main__':unittest.main()
