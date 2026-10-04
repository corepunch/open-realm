"""Reject incomplete or altered portal captures and pin production fixtures."""
import ast,copy,hashlib,json,re,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_gate_traversal_trace import verify

class GateTraversalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-traversal-live-1.27.json').read_text())

    def test_live_contract_rejects_missing_altered_or_duplicate_evidence(self):
        fixture=self.fixture;capture=fixture['captures'][0]
        rows=[dict(event='metadata',**capture['metadata'])]+copy.deepcopy(fixture['observations'])+[dict(event='trace-end',installed=True)]
        self.assertEqual(verify(rows,fixture,capture),fixture['observations'])
        for change in ('footer','installed','complete','duplicate','source','motion','route','consumer','truncated','error'):
            bad=copy.deepcopy(rows)
            if change=='footer':bad.pop()
            elif change=='installed':bad[-1]['installed']=False
            elif change=='complete':bad=[r for r in bad if r.get('value')!=fixture['completion']]
            elif change=='duplicate':bad.insert(-1,dict(event='marker',value=fixture['completion']))
            elif change=='source':bad[0]['source_sha256']['map']='0'*64
            elif change=='motion':next(r for r in bad if r.get('event')=='velocity-commit')['after'][4]^=1
            elif change=='route':next(r for r in bad if r.get('event')=='route')['points'][0][0]+=1
            elif change=='consumer':next(r for r in bad if r.get('event')=='gate-consumer')['index']+=1
            elif change=='truncated':bad.pop(3)
            else:bad.append(dict(event='trace-failed'))
            with self.subTest(change=change),self.assertRaises(ValueError):verify(bad,fixture,capture)

    def test_engine_fixture_matches_every_original_motion_word_and_producer(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_gate_traversal.h').read_text()
        array=source.split('retail_gate95_motion[][7]={',1)[1].split('};',1)[0]
        expected=[]
        for row in self.fixture['observations']:
            if row['event']=='velocity-commit':
                after=row['after'];expected.extend([0,after[0],after[2],after[3],after[4],after[5],after[7]])
        self.assertEqual([int(v,16)for v in re.findall(r'0x([0-9a-f]+)u',array)],expected)
        body=''.join(ast.literal_eval(s)for s in re.findall(r'"(?:[^"\\]|\\.)*"',source.split('retail_gate95_script[]=')[1]))
        # The engine fixture supplies common.j's missing constant and main entry.
        body=body.replace(' constant integer PLAYER_NEUTRAL_PASSIVE=15\n','')
        body=body.split('\nfunction main takes nothing returns nothing\n')[0]
        producer=(ROOT/'tools/frida/wc3_waygate_traversal_probe.j').read_bytes()
        self.assertEqual(body.encode(),producer)
        self.assertEqual(hashlib.sha256(producer).hexdigest(),self.fixture['producer_sha256'])
        for kind in ('edges','consumer'):
            self.assertEqual(hashlib.sha256((ROOT/f'tools/ghidra/fixtures/retail-gate-{kind}-1.27.json').read_bytes()).hexdigest(),self.fixture[kind+'_sha256'])
        observer=(ROOT/'tools/frida/wc3_pathfinding.js').read_bytes()
        self.assertEqual(hashlib.sha256(observer).hexdigest(),self.fixture['captures'][0]['metadata']['source_sha256']['wc3_pathfinding.js'])

if __name__=='__main__':unittest.main()
