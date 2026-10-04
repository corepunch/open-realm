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

class GateLifetimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixtures={label:json.loads((ROOT/f'tools/ghidra/fixtures/retail-gate-lifetime-{label}-live-1.27.json').read_text()) for label in ('same','reuse')}

    def test_destroy_and_reuse_captures_reject_pool_marker_route_and_motion_mutations(self):
        for label,fixture in self.fixtures.items():
            capture=fixture['captures'][0]
            rows=[dict(event='metadata',**capture['metadata'])]+copy.deepcopy(fixture['observations'])+[dict(event='trace-end',installed=True)]
            self.assertEqual(verify(rows,fixture,capture),fixture['observations'])
            for change in ('footer','complete','duplicate','source','release','allocation','availability','marker','motion','route','consumer','error'):
                bad=copy.deepcopy(rows)
                if change=='footer':bad.pop()
                elif change=='complete':bad=[r for r in bad if r.get('value')!=fixture['completion']]
                elif change=='duplicate':bad.insert(-1,dict(event='marker',value=fixture['completion']))
                elif change=='source':bad[0]['source_sha256']['map']='0'*64
                elif change=='release':bad=[r for r in bad if r.get('event')!='gate-pool-release']
                elif change=='allocation':next(r for r in bad if r.get('event')=='gate-pool-allocate')['id']+=1
                elif change=='availability':next(r for r in bad if r.get('event')=='gate-pool-release')['after']['used'][1]^=1
                elif change=='marker':next(r for r in bad if r.get('event')=='gate-marker-publish')['maps'][0]['markers'][0]^=1
                elif change=='motion':next(r for r in bad if r.get('event')=='velocity-commit')['after'][4]^=1
                elif change=='route':next(r for r in bad if r.get('event')=='route')['points'][0][0]+=1
                elif change=='consumer':next(r for r in bad if r.get('event')=='gate-consumer')['index']+=1
                else:bad.append(dict(event='trace-failed'))
                with self.subTest(label=label,change=change),self.assertRaises(ValueError):verify(bad,fixture,capture)

    def test_same_callback_replacement_allocates_before_release_and_never_warps(self):
        rows=self.fixtures['same']['observations']
        allocations=[r for r in rows if r['event']=='gate-pool-allocate']
        releases=[r for r in rows if r['event']=='gate-pool-release']
        self.assertEqual([r['id'] for r in allocations],[1,1,2])
        self.assertLess(next(i for i,r in enumerate(rows) if r is allocations[-1]),next(i for i,r in enumerate(rows) if r is releases[-1]))
        self.assertEqual(allocations[-1]['before']['used'][1],1)
        self.assertEqual(allocations[-1]['after']['used'][1:3],[1,1])
        self.assertEqual(releases[-1]['after']['used'][1:3],[0,1])
        self.assertEqual(sum(r['event']=='gate-traversal' for r in rows),0)
        self.assertEqual(sum(r['event']=='velocity-commit' for r in rows),514)

    def test_delayed_replacement_reuses_id_and_preserves_cached_exit(self):
        rows=self.fixtures['reuse']['observations']
        allocations=[r for r in rows if r['event']=='gate-pool-allocate']
        releases=[r for r in rows if r['event']=='gate-pool-release']
        self.assertEqual([r['id'] for r in allocations],[1,1,1])
        self.assertLess(next(i for i,r in enumerate(rows) if r is releases[-1]),next(i for i,r in enumerate(rows) if r is allocations[-1]))
        self.assertEqual(allocations[-1]['before']['used'][1],0)
        self.assertEqual(sum(r['event']=='gate-traversal' for r in rows),1)
        self.assertEqual(sum(r['event']=='velocity-commit' for r in rows),299)
        self.assertEqual([r['record'][1:] for r in rows if r['event']=='gate-destination'],[[27,27],[27,27],[19,22]])

    def test_engine_fixture_pins_both_complete_native_journeys_and_producers(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_gate_lifetime.h').read_text()
        for label,fixture in self.fixtures.items():
            array=source.split(f'retail_gate96_{label}_motion[][7]={{',1)[1].split('};',1)[0]
            expected=[word for row in fixture['observations'] if row['event']=='velocity-commit'
                for word in [0,row['after'][0],row['after'][2],row['after'][3],row['after'][4],row['after'][5],row['after'][7]]]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',array)],expected)
            block=source.split(f'retail_gate96_{label}_script[]=',1)[1].split(';',1)[0]
            body=''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"',block))
            body=body.replace(' constant integer PLAYER_NEUTRAL_PASSIVE=15\n','').split('\nfunction main takes nothing returns nothing\n')[0]
            producer=(ROOT/f'tools/frida/wc3_waygate_lifetime_{label}_probe.j').read_bytes()
            self.assertEqual(body.encode(),producer)
            self.assertEqual(hashlib.sha256(producer).hexdigest(),fixture['producer_sha256'])
            self.assertEqual(hashlib.sha256((ROOT/'tools/frida/wc3_pathfinding.js').read_bytes()).hexdigest(),fixture['captures'][0]['metadata']['source_sha256']['wc3_pathfinding.js'])

if __name__=='__main__':unittest.main()
