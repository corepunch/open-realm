"""Frozen original Attack policy and fresh retail repeat/control checks."""
import base64,gzip,json,re,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from verify_attack_cap_live import verify

class AttackExemptionTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        seal=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-exemption-captures-1.27.json.gz').read_bytes()))
        for name,data in seal.items():(self.root/name).write_bytes(base64.b64decode(data))
        self.paths=[self.root/n for n in ('observe-1.jsonl','observe-2.jsonl','control-1.jsonl','control-1-preload.txt')]

    def mutate(self,predicate,edit):
        for p in self.paths[:2]:
            rows=[json.loads(line) for line in p.read_text().splitlines()]
            edit(next(r for r in rows if predicate(r)))
            p.write_text('\n'.join(json.dumps(r) for r in rows)+'\n')

    def test_complete_public_repeats_and_control(self):
        expected=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-exemption-live-1.27.json.gz').read_bytes()))
        self.assertEqual(verify(*self.paths),expected)

    def test_rejects_early_rearm_in_both_repeats(self):
        self.mutate(lambda r:r['event']=='begin' and r['tick']==11,
                    lambda r:r['after'].update(deadline=r['after']['deadline']+1))
        with self.assertRaisesRegex(ValueError,'held deadline'):verify(*self.paths)

    def test_rejects_wrong_deadline_in_both_repeats(self):
        self.mutate(lambda r:r['event']=='begin' and r['tick']==10,
                    lambda r:r['after'].update(deadline=r['after']['deadline']+1))
        with self.assertRaisesRegex(ValueError,'arm deadline'):verify(*self.paths)

    def test_rejects_post_positive_damage_substitution(self):
        self.mutate(lambda r:r['event']=='notice',lambda r:r.update(damage=1))
        with self.assertRaisesRegex(ValueError,'zero damage'):verify(*self.paths)

    def test_rejects_incomplete_capture(self):
        self.mutate(lambda r:r['event']=='trace-end',lambda r:r['counts'].update(begin=7))
        with self.assertRaisesRegex(ValueError,'truncated'):verify(*self.paths)

    def test_rejects_changed_control(self):
        self.paths[3].write_bytes(self.paths[3].read_bytes()+b'changed')
        with self.assertRaisesRegex(ValueError,'control file'):verify(*self.paths)

    def test_original_notification_guards_use_unit_status_at_5c(self):
        fixture=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-exemption-1.27.json.gz').read_bytes()))
        self.assertEqual(len(fixture['notification']),72)
        for row in fixture['notification']:
            disabled,flags,suspension,source,packet_flags=row['input']
            self.assertEqual(row['accepted'],not disabled and not flags and not suspension and source and not(packet_flags&2))

    def test_original_timer_and_regroup_words_match_generated_header(self):
        fixture=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-attack-exemption-1.27.json.gz').read_bytes()))
        text=(ROOT/'games/warcraft-3/game/tests/retail_attack_exemption.h').read_text()
        table=text.split('retail_attack_exemption[] = {',1)[1].split('};',1)[0]
        words=[[int(a),int(b,16),int(c,16),int(d)] for a,b,c,d in re.findall(r'\{(\d+)u,0x([0-9a-f]+)u,0x([0-9a-f]+)u,(\d+)u\}',table)]
        self.assertEqual(words,[[int(r['active'] and not r['cancelled']),r['deadline'],r['now'],int(r['armed'])] for r in fixture['timer']])
        table=text.split('retail_attack_group_status[] = {',1)[1].split('};',1)[0]
        words=[[int(a,16),*[int(v) for v in rest]] for a,*rest in re.findall(r'\{0x([0-9a-f]+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u\}',table)]
        expected=[]
        for r in fixture['consumers']:
            flags,cooldown,first,second,exempt,path,near=r['input']
            if flags in (0,4,0x100) and first in (0,0x10000) and second==0 and path==0:
                expected.append([flags,cooldown,int(first!=0),exempt,near,r['output'][2]])
        self.assertEqual(len(expected),72);self.assertEqual(words,expected)

if __name__=='__main__':unittest.main()
