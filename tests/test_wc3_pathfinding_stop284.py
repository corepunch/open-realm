"""Reject missing or reordered retail Stop observations and engine projections."""
import copy, gzip, json, sys, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_stop284 as v


class StopChronologyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.s=json.loads(v.SPEC.read_text())
        cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def rejected(self, mutate):
        b=copy.deepcopy(self.b)
        mutate(b)
        with self.assertRaises(ValueError):v.validate_runtime(b,self.s)

    def test_complete_pinned_contract(self):v.validate(self.s)

    def test_missing_repeat(self):self.rejected(lambda b:b['captures'].pop())

    def test_instrumented_control(self):
        self.rejected(lambda b:b['captures'][2]['rows'].insert(1,dict(seq=0,event='unit-stop-enter')))

    def test_lost_second_recovery(self):
        self.rejected(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('seq')==28)))

    def test_recovery_before_callback(self):
        def edit(b):
            rows=b['captures'][0]['rows']
            p=next(i for i,r in enumerate(rows)if r.get('value','').startswith('S284 tick=1 scene=0 label=unit-event'))
            rows[p],rows[p+1]=rows[p+1],rows[p]
        self.rejected(edit)

    def test_wrong_recovery_caller(self):
        self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='unit-stop-enter').update(caller='0x5fa8a5'))

    def test_control_pose_changed(self):
        self.rejected(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('x=624.000','x=656.000')))

    def test_non_readonly_capture(self):
        self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))

    def test_native_event_lost(self):
        self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='move-dispatch-enter').update(task_event=0))

    def test_projection_is_retail_export(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/fixtures/retail_stop284.h').read_text(),v.header(self.s))

    def test_old_geometry_fixtures_unchanged(self):
        # Retained full bridge and structure predicates still validate their
        # own pinned sources and immutable original numeric expectations.
        import verify_wc3_pathing_work258 as prior
        spec=json.loads(prior.FIXTURE.read_text())
        bundle=json.loads(gzip.decompress(prior.BUNDLE.read_bytes()))
        self.assertEqual(prior.runtime(bundle,spec)['scope_events'],68)
        self.assertEqual(len(bundle['original']),16)


if __name__=='__main__':unittest.main()
