"""Frozen retail speed policy and live witnesses reject corrupted evidence."""
import base64,copy,gzip,json,re,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from verify_group_speed_live import verify

class GroupSpeedTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        seal=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-group-speed-captures-1.27.json.gz').read_bytes()))
        for name,data in seal.items():(self.root/name).write_bytes(base64.b64decode(data))
        self.paths=[self.root/n for n in ('a-observe-1.jsonl','a-observe-2.jsonl','a-control-1.jsonl','a-control-1-preload.txt')]

    def mutate(self,predicate,edit,both=True):
        for path in self.paths[:2] if both else self.paths[:1]:
            rows=[json.loads(l) for l in path.read_text().splitlines()]
            row=next(r for r in rows if predicate(r));edit(row)
            path.write_text('\n'.join(json.dumps(r) for r in rows)+'\n')

    def test_complete_repeat_and_observer_free_control(self):
        expected=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-group-speed-live-1.27.json.gz').read_bytes()))
        self.assertEqual(verify(*self.paths),expected)

    def test_rejects_speed_word_changed_in_both_repeats(self):
        self.mutate(lambda r:r['event']=='commit' and r.get('target') and r.get('speed')==0x408e8000,
                    lambda r:r.update(speed=0x408e8001))
        with self.assertRaisesRegex(ValueError,'target scale'):verify(*self.paths)

    def test_rejects_hidden_adjustment(self):
        self.mutate(lambda r:r['event']=='commit' and r.get('speed')==0x408e8000,
                    lambda r:r.update(unseen=1))
        with self.assertRaisesRegex(ValueError,'prerequisites'):verify(*self.paths)

    def test_rejects_missing_completion(self):
        self.mutate(lambda r:r['event']=='marker' and 'label=complete' in r['value'],
                    lambda r:r.update(value='G032 tick=650 label=partial'))
        with self.assertRaisesRegex(ValueError,'completion'):verify(*self.paths)

    def test_rejects_observer_control_change(self):
        self.paths[3].write_bytes(self.paths[3].read_bytes()+b'corruption')
        with self.assertRaisesRegex(ValueError,'control file'):verify(*self.paths)

    def test_core_fixture_matches_compiled_expectations(self):
        frozen=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-group-target-speed-1.27.json.gz').read_bytes()))
        text=(ROOT/'games/warcraft-3/game/tests/retail_group_target_speed.h').read_text()
        words=[int(x,16) for x in re.findall(r'0x([0-9a-f]{8})u',text)]
        self.assertEqual(len(frozen['cases']),6480)
        self.assertEqual(words,[w for c in frozen['cases'] for w in c['input']+[c['output']]])

if __name__=='__main__':unittest.main()
