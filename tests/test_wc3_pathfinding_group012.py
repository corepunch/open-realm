"""Keep original GROUP-01.2 membership transitions and route ownership executable."""
import base64,copy,gzip,hashlib,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_group012 as V
class Group012Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);spec=copy.deepcopy(self.spec)
        rows=[json.loads(line)for line in base64.b64decode(bundle['captures'][0]['raw_base64']).decode().splitlines()]
        change(rows)
        # Rehash the changed raw input so semantic rejection is independently
        # exercised, rather than only checking a fixed compressed-file digest.
        raw=('\n'.join(json.dumps(row)for row in rows)+'\n').encode()
        bundle['captures'][0]['raw_base64']=base64.b64encode(raw).decode()
        spec['captures'][0]['raw_sha256']=hashlib.sha256(raw).hexdigest()
        with self.assertRaises(ValueError):V.validate_runtime(bundle,spec)
    def test_original_inputs_and_contract(self):
        result=V.validate(self.spec);self.assertEqual(result['physical_owners_per_capture'],4)
        self.assertEqual(result['transitions_per_capture'],9)
    def test_missing_new_engine_ledger(self):
        spec=copy.deepcopy(self.spec);spec['engine_tests'].pop(0)
        with self.assertRaises(ValueError):V.validate(spec)
    def test_changed_transition(self):
        self.reject(lambda rows:next(r for r in rows if r.get('event')=='move-group-bind' and r['before']!=r['after']).update(after=[-1,-1]))
    def test_missing_binding_row(self):
        self.reject(lambda rows:rows.pop(next(i for i,r in enumerate(rows)if r.get('event')=='move-group-bind' and r['before']!=r['after'])))
    def test_wrong_caller(self):
        self.reject(lambda rows:next(r for r in rows if r.get('event')=='move-group-bind' and r['before']!=r['after']).update(caller=0))
    def test_aliased_owned_path(self):
        def change(rows):
            paths=[r for r in rows if r.get('event')=='group-target'];paths[1]['path']=paths[0]['path']
        self.reject(change)
    def test_missing_owned_path(self):
        self.reject(lambda rows:rows.pop(next(i for i,r in enumerate(rows)if r.get('event')=='group-target')))
    def test_raw_ordinal_not_silently_dropped(self):
        self.reject(lambda rows:rows.insert(1,{'event':'unrelated-retained-row'}))
    def test_alias_map_not_silently_dropped(self):
        spec=copy.deepcopy(self.spec);spec['captures'][0]['projection']['mover_pointer_aliases']={}
        with self.assertRaises(ValueError):V.validate_runtime(self.bundle,spec)
    def test_incomplete_original_capture(self):
        self.reject(lambda rows:rows[-1].update(installed=False))
    def test_missing_public_completion(self):
        self.reject(lambda rows:rows.__setitem__(slice(None),[r for r in rows if not(r.get('event')=='marker' and 'label=complete 'in r.get('value',''))]))
if __name__=='__main__':unittest.main()
