"""Keep shared-owner movement words, member identity and producer provenance exact."""
import ast,copy,hashlib,json,re,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_gate_traversal_trace import verify

class RandomMovementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-random-movement-live-1.27.json').read_text())

    def test_full_stream_rejects_changed_draws_candidates_and_transactions(self):
        fixture=self.fixture
        for capture in fixture['captures']:
            observed=copy.deepcopy(fixture['observations']);movers=capture['motion_movers']
            for row in observed:
                if 'member' in row:row['mover']=movers[row['member']]
                if row['event']=='separation-query':
                    row['source']=movers[row.pop('source_member')]
                    row['members']=[movers[i] for i in row['members']]
            rows=[dict(event='metadata',**capture['metadata'])]+observed+[dict(event='trace-end',installed=True)]
            self.assertEqual(verify(rows,fixture,capture),fixture['observations'])
            for event,field in [('overlap-draw','direction'),('retry-result','ownerAfter'),
                    ('retry-init','ownerBefore'),('separation-owner','after'),('separation-query','members')]:
                bad=copy.deepcopy(rows);row=next(r for r in bad if r.get('event')==event)
                if field=='members':row[field]=['0xunknown']
                else:row[field][0]^=1
                with self.subTest(event=event),self.assertRaises(ValueError):verify(bad,fixture,capture)
            bad=copy.deepcopy(rows);i=next(i for i,r in enumerate(bad) if r.get('event')=='retry-result');bad.pop(i)
            with self.assertRaises(ValueError):verify(bad,fixture,capture)
            bad=copy.deepcopy(rows);next(r for r in bad if r.get('event')=='separation-owner')['mover']='0xunknown'
            with self.assertRaises(ValueError):verify(bad,fixture,capture)

    def test_engine_fixture_retains_all_motion_retry_and_separation_words(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_random_interleave.h').read_text();obs=self.fixture['observations']
        expected={
            'motion':[[r['member'],r['after'][0],r['after'][2],r['after'][3],r['after'][4],r['after'][5],r['after'][7]] for r in obs if r['event']=='velocity-commit'],
            'retry':[[r['member'],r['counter'],*r['nativeSource'],*r['nativeGoal'],r['members'],r['before'],r['after'],r['result'],*r['ownerBefore'],*r['ownerAfter']] for r in obs if r['event']=='retry-result'],
            'repulse':[[r['member'],r['counter'],*r['before'],*r['position'],*r['ownerBefore'],*r['after'],*r['afterPosition'],*r['ownerAfter']] for r in obs if r['event']=='separation-owner']}
        for name,rows in expected.items():
            block=source.split(f'random_interleave_{name}[][{len(rows[0])}]={{',1)[1].split('};',1)[0]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',block)],[v for row in rows for v in row])
        block=source.split('random_interleave_script[]=',1)[1].split(';',1)[0]
        body=''.join(ast.literal_eval(v) for v in re.findall(r'"(?:[^"\\]|\\.)*"',block))
        body=body.replace(' constant integer PLAYER_NEUTRAL_PASSIVE=15\n','').split('\nfunction main takes nothing returns nothing\n')[0]
        producer=(ROOT/'tools/frida/wc3_random_movement_probe.j').read_bytes()
        self.assertEqual(body.encode(),producer);self.assertEqual(hashlib.sha256(producer).hexdigest(),self.fixture['producer_sha256'])
        for capture in self.fixture['captures']:
            for path in ('trace_wc3_pathfinding.py','wc3_pathfinding.js','wc3_pathfinding_random_movement.js'):
                self.assertEqual(hashlib.sha256((ROOT/'tools/frida'/path).read_bytes()).hexdigest(),capture['metadata']['source_sha256'][path])

    def test_both_movers_preserve_the_shared_draw_order_and_no_draw_countdowns(self):
        state=[4273436052,209508436];draws=0;owners=set();overlaps=0
        for row in self.fixture['observations']:
            if row['event']=='overlap-draw':
                self.assertEqual(row['before'],state);state=row['after'];draws+=1;overlaps+=1
            elif row['event']=='retry-result':
                self.assertEqual(row['ownerBefore'],state);owners.add(row['member'])
                if row['before']:
                    self.assertEqual(row['ownerAfter'],state)
                    self.assertEqual(row['after'],row['before']-(row['before']>1))
                else:
                    self.assertNotEqual(row['ownerAfter'],state);draws+=1
                state=row['ownerAfter']
        self.assertEqual(overlaps,13);self.assertEqual(draws,48);self.assertEqual(owners,{0,1})
        self.assertEqual(state,[3870341697,146278604])
        self.assertEqual(self.fixture['counts']['separation-owner'],830)
        self.assertEqual(self.fixture['counts']['velocity-commit'],1361)
