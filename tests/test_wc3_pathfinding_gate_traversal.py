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

class GateExitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixtures={label:json.loads((ROOT/f'tools/ghidra/fixtures/retail-gate-exit-{label}-live-1.27.json').read_text()) for label in ('near','sealed')}

    def test_exit_captures_reject_incomplete_or_altered_admission_and_retry(self):
        for label,fixture in self.fixtures.items():
            for capture in fixture['captures']:
                rows=[dict(event='metadata',**capture['metadata'])]+copy.deepcopy(fixture['observations'])+[dict(event='trace-end',installed=True)]
                self.assertEqual(verify(rows,fixture,capture),fixture['observations'])
                for change in ('footer','complete','duplicate','source','result','destination','consumer','motion','route','error'):
                    bad=copy.deepcopy(rows)
                    if change=='footer':bad.pop()
                    elif change=='complete':bad=[r for r in bad if r.get('value')!=fixture['completion']]
                    elif change=='duplicate':bad.insert(-1,dict(event='marker',value=fixture['completion']))
                    elif change=='source':bad[0]['source_sha256']['map']='0'*64
                    elif change=='result':next(r for r in bad if r.get('event')=='gate-traversal')['result']^=1
                    elif change=='destination':next(r for r in bad if r.get('event')=='gate-traversal')['destination'][0]+=1
                    elif change=='consumer':next(r for r in bad if r.get('event')=='gate-consumer')['index']+=1
                    elif change=='motion':next(r for r in bad if r.get('event')=='velocity-commit')['after'][7]^=1
                    elif change=='route':next(r for r in bad if r.get('event')=='route')['points'][0][0]+=1
                    else:bad.append(dict(event='trace-failed'))
                    with self.subTest(label=label,change=change),self.assertRaises(ValueError):verify(bad,fixture,capture)

    def test_surrounded_exit_retains_cached_crossing_after_all_eight_rejections(self):
        rows=self.fixtures['sealed']['observations']
        attempts=[r for r in rows if r['event']=='gate-traversal']
        self.assertEqual(len(attempts),8)
        self.assertTrue(all(r['result']==0 and r['destination']==[54.5,55.5] for r in attempts))
        failures=[r for r in rows if r['event']=='gate-consumer' and r['before']['execute']==1]
        self.assertEqual(len(failures),8)
        self.assertTrue(all(r['index']==r['before']['index']==2 and r['result']==r['warped']==0 for r in failures))
        self.assertEqual(sum(r['event']=='velocity-commit' for r in rows),712)

    def test_blocked_requested_cell_places_nearby_and_outside_routes_do_not_warp(self):
        rows=self.fixtures['near']['observations']
        attempts=[r for r in rows if r['event']=='gate-traversal']
        self.assertEqual(len(attempts),1)
        self.assertEqual(attempts[0]['result'],1)
        # Traversal logs the requested point; complete motion proves admitted pose.
        self.assertEqual(attempts[0]['destination'],[54.5,55.5])
        self.assertEqual(sum(r['event']=='velocity-commit' for r in rows),568)
        for label,fixture in self.fixtures.items():
            for marker in ('gate_outside_fresh_exit','gate_negative_fresh_exit'):
                index=next(i for i,r in enumerate(fixture['observations']) if r['event']=='marker' and marker in r['value'])
                self.assertFalse(any(r['event']=='gate-traversal' for r in fixture['observations'][index:]))

    def test_engine_fixture_pins_complete_exit_motion_and_original_producers(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_gate_exit.h').read_text()
        for label,fixture in self.fixtures.items():
            array=source.split(f'retail_gate97_{label}_motion[][7]={{',1)[1].split('};',1)[0]
            expected=[word for row in fixture['observations'] if row['event']=='velocity-commit'
                for word in [0,row['after'][0],row['after'][2],row['after'][3],row['after'][4],row['after'][5],row['after'][7]]]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',array)],expected)
            block=source.split(f'retail_gate97_{label}_script[]=',1)[1].split(';',1)[0]
            body=''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"',block))
            body=body.replace(' constant integer PLAYER_NEUTRAL_PASSIVE=15\n','')
            body=body.replace(' constant pathingtype PATHING_TYPE_WALKABILITY=ConvertPathingType(1)\n','').split('\nfunction main takes nothing returns nothing\n')[0]
            producer=(ROOT/f'tools/frida/wc3_waygate_exit_{label}_probe.j').read_bytes()
            self.assertEqual(body.encode(),producer)
            self.assertEqual(hashlib.sha256(producer).hexdigest(),fixture['producer_sha256'])
            self.assertEqual(hashlib.sha256((ROOT/'tools/frida/wc3_pathfinding.js').read_bytes()).hexdigest(),fixture['captures'][0]['metadata']['source_sha256']['wc3_pathfinding.js'])

class MultipleGateTests(unittest.TestCase):
    def test_group_member_identity_and_complete_chain_are_strict(self):
        for label in ('group-open','group-wall','chain','eligibility'):
            fixture=json.loads((ROOT/f'tools/ghidra/fixtures/retail-gate-{label}-live-1.27.json').read_text())
            for capture in fixture['captures']:
                motion=copy.deepcopy(fixture['observations'])
                for row in motion:
                    if 'member' in row:row['mover']=capture['motion_movers'][row['member']]
                rows=[dict(event='metadata',**capture['metadata'])]+motion+[dict(event='trace-end',installed=True)]
                self.assertEqual(verify(rows,fixture,capture),fixture['observations'])
                for change in ('completion','motion','consumer','route','unmapped','swapped','missing','mapping'):
                    if label in ('chain','eligibility') and change in ('unmapped','swapped','missing','mapping'):continue
                    bad=copy.deepcopy(rows);contract=copy.deepcopy(capture)
                    if change=='completion':bad=[r for r in bad if r.get('value')!=fixture['completion']]
                    elif change=='motion':next(r for r in bad if r.get('event')=='velocity-commit')['after'][4]^=1
                    elif change=='consumer':next(r for r in bad if r.get('event')=='gate-consumer')['index']+=1
                    elif change=='route':next(r for r in bad if r.get('event')=='route')['points'][0][0]+=1
                    elif change=='mapping':contract['motion_movers'][1]=contract['motion_movers'][0]
                    else:
                        row=next(r for r in bad if r.get('event')=='velocity-commit')
                        if change=='missing':row.pop('mover')
                        elif change=='unmapped':row['mover']='0xunknown'
                        else:row['mover']=capture['motion_movers'][1-row['member']]
                    with self.subTest(label=label,change=change),self.assertRaises(ValueError):verify(bad,fixture,contract)

    def test_complete_native_motion_and_producers_pin_engine_inputs(self):
        for label,header,symbol,producer in (
                ('group-open','group','gate98_open','group_open'),
                ('group-wall','group','gate98_wall','group_wall'),
                ('chain','chain','gate99','chain'),
                ('eligibility','eligibility','gate100','eligibility')):
            fixture=json.loads((ROOT/f'tools/ghidra/fixtures/retail-gate-{label}-live-1.27.json').read_text())
            source=(ROOT/f'games/warcraft-3/game/tests/retail_gate_{header}.h').read_text()
            array=source.split(f'retail_{symbol}_motion[][7]={{',1)[1].split('};',1)[0]
            expected=[word for row in fixture['observations'] if row['event']=='velocity-commit'
                for word in [row.get('member',0),row['after'][0],row['after'][2],row['after'][3],row['after'][4],row['after'][5],row['after'][7]]]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',array)],expected)
            block=source.split(f'retail_{symbol}_script[]=',1)[1].split(';',1)[0]
            body=''.join(ast.literal_eval(v) for v in re.findall(r'"(?:[^"\\]|\\.)*"',block))
            body=body.replace(' constant integer PLAYER_NEUTRAL_PASSIVE=15\n','').split('\nfunction main takes nothing returns nothing\n')[0]
            raw=(ROOT/f'tools/frida/wc3_waygate_{producer}_probe.j').read_bytes()
            self.assertEqual(body.encode(),raw)
            self.assertEqual(hashlib.sha256(raw).hexdigest(),fixture['producer_sha256'])

    def test_original_equal_cost_routes_keep_deterministic_gate_choice(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-multiple-gates-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),512)
        self.assertEqual(len(fixture['publications']),8)
        controls=[r for r in fixture['cases'] if r['topology']=='equal' and r['lane']==0 and r['size_input']==0 and r['budget']==400 and r['warp']==1]
        self.assertEqual(len(controls),8)
        for row in controls:
            self.assertEqual(row['distance'],59 if not row['active'] else 58)
            self.assertEqual(row['route_warps'],0 if not row['active'] else 1)
            if row['active']==3:
                sentinel=row['route_words'].index(0xc7fa0001)
                self.assertEqual(row['route_words'][sentinel+1],0x3f800000)

if __name__=='__main__':unittest.main()

class GateThresholdTests(unittest.TestCase):
    def test_equality_next_scalar_and_failed_crossing_retain_native_state(self):
        cases=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-threshold-1.27.json').read_text())['cases']
        self.assertEqual(len(cases),320)
        selected=[r for r in cases if r['point']==[0,0] and r['index']==2 and not r['force'] and r['active']==1]
        self.assertEqual(len(selected),10)
        for row in selected:
            admitted=row['source'][0]<=0x3efae148
            self.assertEqual(row['result'],(1 if row['placement_result'] else 2) if admitted else 0)
            self.assertEqual(row['next_index'],0 if admitted and row['placement_result'] else 2)
            self.assertEqual(row['fine_index'],0xffffffff if admitted and row['placement_result'] else 7)
            self.assertEqual(row['delay'],20 if admitted and not row['placement_result'] else 5)
            self.assertEqual(len(row['placed']),int(admitted))
        for row in cases:
            if not row['index']:self.assertEqual((row['result'],row['next_index'],row['placed']),(0,0,[]))

    def test_public_flight_lifecycle_has_three_crossings_and_disabled_direct_control(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-eligibility-live-1.27.json').read_text())['observations']
        self.assertEqual(sum(r['event']=='velocity-commit' for r in rows),383)
        self.assertEqual(sum(r['event']=='gate-traversal' for r in rows),3)
        start=next(i for i,r in enumerate(rows) if r['event']=='marker' and 'gate_rebound_flight' in r['value'])
        end=next(i for i,r in enumerate(rows) if r['event']=='marker' and 'gate_restored_ground' in r['value'])
        self.assertTrue(any(r['event']=='velocity-commit' for r in rows[start:end]))
        self.assertFalse(any(r['event']=='gate-traversal' for r in rows[start:end]))
        self.assertFalse(any(r['event']=='gate-consumer' for r in rows[start:end]))
