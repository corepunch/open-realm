"""Retail help admission, geometry witnesses and strict repeat/control checks."""
import base64,gzip,json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from verify_attack_help_live import verify

class AttackHelpTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        seal=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-help-captures-1.27.json.gz').read_bytes()))
        for name,data in seal.items():(self.root/name).write_bytes(base64.b64decode(data))
        self.permission=[self.root/n for n in ('observe-v2-1.jsonl','observe-v2-2.jsonl','control-v2-1.jsonl','control-v2-1-preload.txt')]
        self.help=[self.root/n for n in ('help-pose-observe-1.jsonl','help-pose-observe-2.jsonl','help-v2-control-1.jsonl','help-v2-control-1-preload.txt')]

    def mutate(self,paths,predicate,edit):
        for path in paths[:2]:
            rows=[json.loads(line) for line in path.read_text().splitlines()]
            edit(next(row for row in rows if predicate(row)))
            path.write_text('\n'.join(json.dumps(r) for r in rows)+'\n')

    def test_complete_directional_repeat_and_control(self):
        expected=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-help-permissions-1.27.json.gz').read_bytes()))
        self.assertEqual(verify(*self.permission),expected)

    def test_complete_radius_cooldown_pose_repeat_and_control(self):
        expected=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-help-radius-1.27.json.gz').read_bytes()))
        self.assertEqual(verify(*self.help),expected)
        self.assertEqual([r['helper']['fine'] for r in expected['events'] if r['event']=='candidate' and r['tick']==10],
                         [[1105985536,1102839808],[1105461248,1104936960],
                          [1106509824,1104936960],[1106411520,1103626240],[1103626240,1103626240]])

    def test_rejects_hardcoded_stock_radius(self):
        self.mutate(self.help,lambda r:r['event']=='help-radius',lambda r:r.update(radius=1142292480))
        with self.assertRaisesRegex(ValueError,'radius/cooldown'):verify(*self.help)

    def test_rejects_ordinary_delay_applied_to_ai(self):
        self.mutate(self.help,lambda r:r['event']=='help-arm' and r['victim']['owner']==12,
                    lambda r:r.update(delay=1077936128))
        with self.assertRaisesRegex(ValueError,'radius/cooldown'):verify(*self.help)

    def test_rejects_truncated_delivery(self):
        self.mutate(self.help,lambda r:r['event']=='trace-end',lambda r:r['counts'].update(candidate=29))
        with self.assertRaisesRegex(ValueError,'truncated'):verify(*self.help)

    def test_rejects_changed_control_file(self):
        self.help[3].write_bytes(self.help[3].read_bytes()+b'changed')
        with self.assertRaisesRegex(ValueError,'control file'):verify(*self.help)

    def test_original_ally_callback_covers_all_native_guard_states(self):
        fixture=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-help-policy-1.27.json.gz').read_bytes()))
        self.assertEqual(fixture['cases'],192);self.assertEqual(fixture['notified'],12);self.assertEqual(fixture['accepted'],1)
        for row in fixture['rows']:
            request,response,passive,engaged,disabled,flags,suspended=row['input']
            self.assertEqual(row['notified'],bool(request and response and not passive and not engaged))
            self.assertEqual(row['accepted'],bool(row['notified'] and not disabled and not flags and not suspended))

if __name__=='__main__':unittest.main()
