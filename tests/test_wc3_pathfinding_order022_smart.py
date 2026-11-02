"""Keep direct Smart identity evidence separate from physical Attack and forged controls."""
import base64
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_order022_smart as v


class SmartHeadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index, rows=None, preload=None):
        bundle = copy.deepcopy(self.bundle)
        item = bundle['captures'][index]
        values = [json.loads(line) for line in base64.b64decode(item['raw_b64']).splitlines()]
        if rows:
            rows(values)
        if preload:
            raw = preload(base64.b64decode(item['preload_b64']).decode()).encode()
            item.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            values[-1]['sha256'] = item['preload_sha256']
        raw = b''.join((json.dumps(row) + '\n').encode() for row in values)
        item.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return bundle

    def test_complete_repeats_and_control(self):
        self.assertEqual(v.audit(), dict(captures=2, controls=1, scenes=15, public_markers=216,
                                        smart_head=851971, attack_head=851983, engine_parity=False, remaining_owner='ORDER-02.2'))

    def test_missing_repeat(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'repeats'):
            v.audit(bundle)

    def test_attack_id_cannot_replace_smart_head(self):
        bundle = self.change(0, preload=lambda s: s.replace('issued event=851971 head=851971', 'issued event=851983 head=851983', 1))
        with self.assertRaisesRegex(ValueError, 'Smart/Attack target notification'):
            v.audit(bundle)

    def test_original_target_is_required(self):
        bundle = self.change(0, preload=lambda s: s.replace('target=2', 'target=7', 1))
        with self.assertRaisesRegex(ValueError, 'Smart/Attack target notification'):
            v.audit(bundle)

    def test_incomplete_probe(self):
        bundle = self.change(2, preload=lambda s: s.replace('label=complete', 'label=incomplete'))
        with self.assertRaisesRegex(ValueError, 'never finished'):
            v.audit(bundle)

    def test_failed_owned_input(self):
        bundle = self.change(0, rows=lambda rr: next(r for r in rr if r['event'] == 'player-input').update(rc=1))
        with self.assertRaisesRegex(ValueError, 'real owned right-click'):
            v.audit(bundle)

    def test_left_click_is_not_smart(self):
        bundle = self.change(0, rows=lambda rr: next(r for r in rr if r['event'] == 'player-input')['cmd'].__setitem__(-1, 'move'))
        with self.assertRaisesRegex(ValueError, 'real owned right-click'):
            v.audit(bundle)

    def test_control_must_be_unhooked(self):
        bundle = self.change(2, rows=lambda rr: rr.insert(1, dict(event='module', seq=1)))
        with self.assertRaisesRegex(ValueError, 'control has observer'):
            v.audit(bundle)

    def test_lost_cast_protection(self):
        def edit(rr):
            next(r for r in rr if r['event'] == 'net-order' and r['fallback'])['state']['head_order']['flags'] = 0
        with self.assertRaisesRegex(ValueError, 'protected original spell'):
            v.audit(self.change(1, rows=edit))

    def test_lost_retained_spell(self):
        def edit(rr):
            packets = [r for r in rr if r['event'] == 'net-order-end']
            packets[-1]['state']['count'] = 1
        with self.assertRaisesRegex(ValueError, 'replaced native spell'):
            v.audit(self.change(0, rows=edit))

    def test_changed_map_artifact(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['map']['raw_b64'] = base64.b64encode(b'wrong map').decode()
        with self.assertRaisesRegex(ValueError, 'map/helper artifact'):
            v.audit(bundle)

    def test_wrong_probe_provenance(self):
        bundle = self.change(0, rows=lambda rr: rr[0]['source_sha256'].__setitem__('order022_smart_probe.j', '0' * 64))
        with self.assertRaisesRegex(ValueError, 'changed source'):
            v.audit(bundle)

    def test_broken_observer_sequence(self):
        def edit(rr):
            next(r for r in rr if r.get('seq') == 3)['seq'] = 5
        with self.assertRaisesRegex(ValueError, 'observer sequence'):
            v.audit(self.change(0, rows=edit))

    def test_control_samples_must_match(self):
        raw = base64.b64decode(self.bundle['captures'][2]['preload_b64']).decode()
        sample = next(line for line in raw.splitlines() if 'label=sample head=851971' in line)
        bundle = self.change(2, preload=lambda s: s.replace(sample, sample.replace('head=851971', 'head=999'), 1))
        with self.assertRaisesRegex(ValueError, 'repeat/control public head'):
            v.audit(bundle)
