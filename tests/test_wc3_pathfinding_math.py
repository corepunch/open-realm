"""Asset-free differential checks of the production C arithmetic, plus trace rejection tests."""
import ctypes
import copy
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_pathing_numeric import add, subtract, multiply, bits, trig_bits, square_root, reciprocal, acos_bits, fractional, modulo, decimal_bits, integer_float, saturating_integer_word, asin_bits, atan_bits, atan2_bits, divide, floor_word, ceil_word, round_word, truncate_word
from generate_wc3_math_tables import sine_table, reciprocal_table, acos_tables
from verify_wc3_motion_trace import verify
from verify_wc3_pathing_integers import integer_literal_word, source_word as integer_source_word
from verify_wc3_pathing_literals import literal_word, source_word
from verify_wc3_pathing_power import corelog, reducedlog, log, exp, power, public as public_power
from verify_wc3_byte_inputs import source_bytes
from verify_wc3_heading_aliases import verify as verify_heading_aliases
from verify_wc3_arrival_trace import verify as verify_arrival, configure as configure_arrival
from verify_wc3_speed_inputs import verify as verify_speed_inputs, digest as speed_digest
from verify_wc3_speed_drop import verify as verify_speed_drop, digest as speed_drop_digest
from verify_wc3_item_speed import verify as verify_item_speed, digest as item_speed_digest
from verify_wc3_axis_position import verify as verify_axis_position, digest as axis_position_digest
from verify_wc3_forced_position import verify as verify_forced_position, digest as forced_position_digest
from verify_wc3_placement_trace import verify as verify_placement, digest as placement_digest
from verify_wc3_stop_recovery_trace import verify as verify_stop_recovery, digest as stop_recovery_digest
from verify_wc3_primary_clock import verify_primary, digest as primary_digest
from verify_wc3_yield_trace import verify as verify_yield
from verify_wc3_wait_heading_trace import verify as verify_wait_heading
from verify_wc3_retry_trace import verify as verify_retry
from verify_wc3_spawn_trace import verify as verify_spawn, digest as spawn_digest
from verify_wc3_spawn_motion_trace import verify as verify_spawn_motion, digest as spawn_motion_digest
from verify_wc3_spawn_phase_trace import verify_births
from verify_wc3_public_oblique_trace import verify_geometry as verify_oblique_geometry


class PathingMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='wc3-pathing-math-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.engines = []
        for opt in ('-O0', '-O2'):
            lib = Path(cls.temp.name) / (opt + '.so')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', opt, '-fPIC', '-shared',
                            '-I', str(ROOT), str(ROOT / 'tools/ghidra/wc3_pathing_engine_probe.c'), '-o', str(lib)], check=True)
            engine = ctypes.CDLL(str(lib))
            engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity_world_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_velocity_heading.argtypes = [ctypes.c_uint32]*3
            engine.pathing_velocity_heading.restype = ctypes.c_uint32
            engine.pathing_velocity.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_integrate.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_heading_error.argtypes = [ctypes.c_uint32]*3
            engine.pathing_heading_error.restype = ctypes.c_uint32
            for name in ('add', 'subtract', 'multiply', 'modulo', 'fractional', 'facing_angle', 'angle', 'sin', 'cos', 'acos', 'sqrt', 'reciprocal'):
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32] * (2 if name in ('add', 'subtract', 'multiply','modulo') else 1)
                proc.restype = ctypes.c_uint32
            cls.engines.append(engine)

    def test_original_formation_destination_distance_and_partial_endpoints(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-formation-destination-1.27.json').read_text())
        maps={}
        for name in ('open','wall','gap'):
            fine=[[int(name!='open' and x==32 and (name=='wall' or not 29<=y<=34)) for x in range(64)] for y in range(64)]
            coarse=[]
            for level in range(4):
                side=32>>level; cells=[]
                for y in range(side):
                    for x in range(side):
                        children=[fine[2*y+dy][2*x+dx] for dy in range(2) for dx in range(2)]
                        if not level: cells.append(1 if all(children) else 2 if any(children) else 0)
                        else: cells.append(children[0] if len(set(children))==1 and children[0]<2 else 2)
                fine=[cells[y*side:(y+1)*side] for y in range(side)]; coarse.extend(cells)
            maps[name]=(ctypes.c_uint8*len(coarse))(*coarse)
        for engine in self.engines:
            for row in fixture['cases']:
                with self.subTest(opt=engine._name,row=row):
                    goal=[8+row['offset'],8]
                    q=(ctypes.c_uint32*8)(32,32,row['size_class']//2,30,*[bits(v) for v in [4,4,goal[0]/2,goal[1]/2]])
                    out=(ctypes.c_uint32*3)(); engine.pathing_adaptive_distance(q,maps[row['fixture']],out)
                    self.assertEqual(out[0],row['query_result'])
                    dest=goal if out[0]<=20 else [struct.unpack('<f',struct.pack('<I',v))[0]*2 for v in out[1:]] if out[0]==0xffffffff else [8,8]
                    self.assertEqual(dest,row['destination'])

    def test_public_oblique_geometry_requires_complete_fine_and_hierarchy_snapshot(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-oblique-1.27.json').read_text())
        geometry=copy.deepcopy(fixture['terrain']['geometry']);geometry.pop('encoding')
        def unpack(value):
            raw=bytes.fromhex(value)
            return [[int.from_bytes(raw[i:i+2],'little'),raw[i+2]] for i in range(0,len(raw),3)]
        geometry['terrainRuns']=unpack(geometry['terrainRuns'])
        geometry['objectRuns']=unpack(geometry['objectRuns'])
        for hierarchy in geometry['hierarchy']:hierarchy['runs']=unpack(hierarchy['runs'])
        rows=[dict(event='metadata',sha256=fixture['binary_sha256'],source_sha256=fixture['terrain']['geometry_capture']['source_sha256']),geometry]
        self.assertTrue(verify_oblique_geometry(rows,fixture)['geometry_verified'])
        self.assertIsInstance(rows[1]['hierarchy'][0]['runs'],list)
        for kind in ('source','missing','duplicate','width','terrain_count','object_mask','hierarchy_count','hierarchy_class'):
            changed=copy.deepcopy(rows);g=changed[-1]
            if kind=='source':changed[0]['source_sha256']['wc3_pathfinding.js']='0'*64
            elif kind=='missing':changed.pop()
            elif kind=='duplicate':changed.append(copy.deepcopy(g))
            elif kind=='width':g['width']+=1
            elif kind=='terrain_count':g['terrainRuns'][0][0]+=1
            elif kind=='object_mask':g['objectRuns'][0][1]=1
            elif kind=='hierarchy_count':g['hierarchy'][0]['runs'][0][0]+=1
            else:g['hierarchy'][0]['runs'][0][1]^=1
            with self.subTest(kind=kind),self.assertRaises(ValueError):verify_oblique_geometry(changed,fixture)

    def test_public_spawn_births_require_primary_dispatch_and_observed_owner_phase(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-spawn-phase-1.27.json').read_text())
        trajectory=json.loads((ROOT/'tools/ghidra/fixtures/retail-primary-clock-trajectory-1.27.json').read_text())
        rows=[];state=[0,0,bits(300)]; owners=0
        for tick in range(3000):
            rows.append(dict(event='clock-source-begin',source='direct',before=[state]))
            if tick and tick%6==0:
                rows.append(dict(event='clock-owner-end')); owners+=1
            for birth in fixture['births']:
                if tick==birth['primary_advances']:
                    for _ in range(2):rows.append(dict(event='position-commit',case=birth['case'],clock=state))
            state=trajectory['advances'][tick]
            rows.append(dict(event='clock-advance-end',domain=20,after=state))
            rows.append(dict(event='clock-source-end',source='direct',after=[state]))
        result=verify_births(rows,fixture)
        self.assertEqual(result['public_births'],8)
        self.assertEqual(result['birth_phases'],[2,0,4,2,0,4,2,0])
        for kind in ('case','clock','missing','outside','owner','advance','first_boundary'):
            changed=copy.deepcopy(rows)
            birth=next(r for r in changed if r['event']=='position-commit' and r['case']=='spawn_1')
            i=changed.index(birth)
            if kind=='case':birth['case']='spawn_2'
            elif kind=='clock':birth['clock'][0]^=1
            elif kind=='missing':changed.remove(birth)
            elif kind=='outside':changed.insert(i,dict(event='clock-source-end',source='direct'))
            elif kind=='owner':changed.insert(i,dict(event='clock-owner-end'))
            elif kind=='advance':changed.insert(i,dict(event='clock-advance-end',domain=20,after=birth['clock']))
            else:changed[i-1],changed[i]=changed[i],changed[i-1]
            with self.subTest(kind=kind),self.assertRaises(ValueError):verify_births(changed,fixture)

    def test_public_spawn_motion_lifetimes_and_rejected_corruption(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-spawn-motion-1.27.json').read_text())
        for engine in self.engines:
            configure_arrival(engine)
            result=verify_spawn_motion(fixture['rows'],engine,fixture)
            self.assertEqual(result['journeys'],8)
            self.assertEqual(result['arrival_evaluations'],247)
            self.assertEqual(result['exact_velocity_commits'],247)
            self.assertEqual(result['final_old_velocity_stops'],8)
            for kind in ('options','identity','stored','predicted','heading','range','result','order','truncation','velocity','motion_identity','motion_order'):
                changed=copy.deepcopy(fixture['rows'])
                a=next(r for r in changed if r['event']=='arrival-evaluation')
                c=next(r for r in changed if r['event']=='velocity-commit')
                if kind=='options':changed[0]['velocityEvents']=False
                elif kind=='identity':a['mover']='0x1'
                elif kind=='stored':a['storedPosition'][0]^=1
                elif kind=='predicted':a['source'][0]^=1
                elif kind=='heading':a['heading']^=1
                elif kind=='range':a['storedRange']^=1
                elif kind=='result':a['result']=1
                elif kind=='order':
                    index=changed.index(a);changed.remove(c);changed.insert(index,c)
                elif kind=='truncation':changed.remove(a)
                elif kind=='motion_identity':next(r for r in changed if r['event']=='motion-decision')['mover']='0x1'
                elif kind=='motion_order':
                    m=next(r for r in changed if r['event']=='motion-decision')
                    changed.remove(m);changed.insert(changed.index(c)+1,m)
                else:c['after'][4]^=1
                # Recompute sequence hashes so real producer/C checks reject it.
                expected=dict(fixture,spawn_sha256=spawn_digest(changed),journey_sha256=spawn_motion_digest(changed))
                with self.subTest(optimization=engine._name,case=kind),self.assertRaises(ValueError):
                    verify_spawn_motion(changed,engine,expected)

    def test_public_spawn_words_and_rejected_incomplete_captures(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-spawn-1.27.json').read_text())
        for engine in self.engines:
            configure_arrival(engine)
            result=verify_spawn(fixture['rows'],engine,fixture)
            self.assertEqual(result['public_create_calls'],8)
            self.assertEqual(result['position_commits'],16)
            self.assertEqual(result['exact_decisions'],239)
            for kind in ('source','input','sentinel','native','commit','candidate','mask','truncation','completion','error'):
                changed=copy.deepcopy(fixture['rows'])
                commit=next(r for r in changed if r['event']=='position-commit')
                native=next(r for r in changed if r['event']=='position-native')
                search=next(r for r in changed if r['event']=='placement-search-end')
                if kind=='source':changed[0]['source_sha256']['map']='z'*64
                elif kind=='input':native['input'][0]^=0x10000
                elif kind=='sentinel':commit['before'][2]^=0x100000
                elif kind=='native':native['output']^=1
                elif kind=='commit':commit['after'][2]^=1
                elif kind=='candidate':search['visits'][-1]['point'][0]+=1
                elif kind=='mask':search['mask']=0
                elif kind=='truncation':changed.remove(commit)
                elif kind=='completion':changed=[r for r in changed if r['event']!='marker']
                else:changed.append(dict(event='error'))
                # Let producer/C checks reject coherent sequence changes too.
                expected=dict(fixture,spawn_sha256=spawn_digest(changed))
                with self.subTest(optimization=engine._name,case=kind),self.assertRaises(ValueError):
                    verify_spawn(changed,engine,expected)

    def test_live_retry_words_and_rejected_incomplete_captures(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-peer-retry-1.27.json').read_text())['rows']
        for engine in self.engines:
            result=verify_retry(rows,engine)
            self.assertEqual(result['counts'],{'retry-init':42,'retry-result':55})
            self.assertEqual(result['digest'],'b22624f045c5916d77deae4f2c07ed815a42a577796e6851175d5eb89331068a')
            for kind in ('source','input','members','owner','result','target','missing','truncation','completion','error'):
                changed=copy.deepcopy(rows)
                init=next(r for r in changed if r['event']=='retry-init')
                advance=next(r for r in changed if r['event']=='retry-result')
                if kind=='source':changed[0]['source_sha256']['map']='z'*64
                elif kind=='input':init['nativeGoal']=init['nativeSource'][:]
                elif kind=='members':del init['members']
                elif kind=='owner':init['ownerAfter'][0]^=1
                elif kind=='result':advance['result']^=1
                elif kind=='target':advance['target'][0]=1
                elif kind=='missing':del init['nativeSource']
                elif kind=='truncation':changed.remove(init)
                elif kind=='completion':changed=[r for r in changed if r['event']!='marker']
                else:changed.append(dict(event='error'))
                with self.subTest(optimization=engine._name,case=kind),self.assertRaises(ValueError):
                    verify_retry(changed,engine)

    def test_complete_retry_native_distance_and_random_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-peer-retry-1.27.json').read_text())
        self.assertEqual(len(frozen['initializations']),336)
        self.assertEqual(len(frozen['advances']),2016)
        for engine in self.engines:
            for name,rows,n,k in [('init',frozen['initializations'],7,3),('advance',frozen['advances'],8,4)]:
                proc=getattr(engine,'pathing_retry_'+name)
                proc.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
                for row in rows:
                    out=(ctypes.c_uint32*k)();proc((ctypes.c_uint32*n)(*row['input']),out)
                    expected=row['output'] if name=='init' else row['output'][:2]+row['output'][-2:]
                    self.assertEqual(list(out),expected)

    def test_live_waiting_caller_words_and_rejected_incomplete_captures(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-wait-heading-1.27.json').read_text())['rows']
        for engine in self.engines:
            result=verify_wait_heading(rows,engine)
            self.assertEqual(result['cases'],424)
            self.assertEqual(result['digest'],'033f2e18400b9ff2bb849d94961744acbeaee40f606f388c9a0fbfc840b3535c')
            for kind in ('source','input','output','gate','advance','missing','counter','truncation','completion','error'):
                changed=copy.deepcopy(rows)
                step=next(r for r in changed if r['event']=='waiting-step')
                if kind=='source':changed[0]['source_sha256']['map']='z'*64
                elif kind=='input':step['source'][0]^=0x00800000
                elif kind=='output':step['nextHeading']^=1
                elif kind=='gate':step['gate']=None
                elif kind=='advance':next(r for r in changed if r['event']=='path-delay')['result']=2
                elif kind=='missing':del step['parameters']
                elif kind=='counter':step['counter']+=1
                elif kind=='truncation':changed.remove(step)
                elif kind=='completion':changed=[r for r in changed if r['event']!='marker']
                else:changed.append(dict(event='error'))
                with self.subTest(optimization=engine._name,case=kind),self.assertRaises(ValueError):
                    verify_wait_heading(changed,engine)

    def test_waiting_caller_matches_complete_original_heading_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-wait-heading-1.27.json').read_text())
        self.assertEqual(len(frozen['rows']),1008)
        for engine in self.engines:
            gate=engine.pathing_yield_advance
            gate.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_uint32]
            gate.restype=ctypes.c_uint32
            for row in frozen['rows']:
                x,y,tx,ty,speed,heading,turn,window,delay=row['input']
                error=engine.pathing_heading_error(engine.pathing_subtract(tx,x),
                                                  engine.pathing_subtract(ty,y),heading)
                motion=(ctypes.c_uint32*7)(speed,heading,error,0,turn,window,1)
                engine.pathing_motion(motion)
                countdown=ctypes.c_uint32(delay)
                self.assertEqual(gate(ctypes.byref(countdown),0),1)
                self.assertEqual([motion[0],motion[1],countdown.value],row['output'])

    def test_moving_yield_matches_original_velocity_and_countdown_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-moving-yield-1.27.json').read_text())
        self.assertEqual(len(frozen['decisions']),8640)
        for engine in self.engines:
            proc=engine.pathing_yield_decision
            proc.argtypes=[ctypes.POINTER(ctypes.c_uint32)];proc.restype=ctypes.c_uint32
            gate=engine.pathing_yield_advance
            gate.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_uint32];gate.restype=ctypes.c_uint32
            for row in frozen['decisions']:
                self.assertEqual(proc((ctypes.c_uint32*10)(*row['input'])),row['decision'])
            for row in frozen['gates']:
                delay=ctypes.c_uint32(row['delay'])
                result=gate(ctypes.byref(delay),bool(row['flags']&0x100000))
                self.assertEqual(delay.value,row['output'])
                self.assertEqual(bool(result),not row['flags']&0x100000 and bool(row['delay']))

    def test_live_ordered_yield_matches_original_and_rejects_incomplete_inputs(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-moving-yield-1.27.json').read_text())['rows']
        for engine in self.engines:
            result=verify_yield(rows,engine)
            self.assertEqual((result['decisions'],result['assignments'],result['delay_calls'],result['duplicate_candidates']),
                             (2503,{4:56,20:10},424,51))
            self.assertEqual(result['digest'],'dc0362228855a17aba4279a9581f9f8a176b8daf8a782f9d1b2d6bbd3fed05fc')
            for kind in ('source','group','blocker','state','writer','gate','truncation','completion','error'):
                changed=copy.deepcopy(rows)
                if kind=='source':changed[0]['source_sha256']['map']='z'*64
                elif kind=='group':
                    row=next(r for r in changed if r['event']=='yield-decision')
                    del row['groups'][row['self']['mover']]
                elif kind=='blocker':
                    next(r for r in changed if r['event']=='yield-decision' and r['blocked'])['blocked']={}
                elif kind=='state':
                    next(r for r in changed if r['event']=='yield-decision')['self']['after']['delay']+=1
                elif kind=='writer':next(r for r in changed if r['event']=='yield-set')['stored']=[0,0]
                elif kind=='gate':next(r for r in changed if r['event']=='path-delay')['after']+=1
                elif kind=='truncation':changed.pop(next(i for i,r in enumerate(changed) if r['event']=='yield-decision'))
                elif kind=='completion':changed=[r for r in changed if r['event']!='marker']
                else:changed.append(dict(event='error'))
                with self.subTest(optimization=engine._name,case=kind),self.assertRaises(ValueError):
                    verify_yield(changed,engine)

    def test_search_obstruction_selects_initial_fine_waypoint(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-long-progress-1.27.json').read_text())
        class FineInput(ctypes.Structure):
            _fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]
        for engine in self.engines:
            proc=engine.pathing_fine_obstruction
            proc.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(FineInput)];proc.restype=ctypes.c_uint32
            for row in frozen['cases']:
                grid=(ctypes.c_uint8*(128*128))(*(2 if row['fixture']=='gap' and x==64 and not 61<=y<=66 else 0 for y in range(128) for x in range(128)))
                for leg in [row,*row['steps']]:
                    values=[ctypes.c_float.from_buffer_copy(ctypes.c_uint32(v)).value for v in leg['source_bits']+leg['fine_words'][:2]]
                    q=(ctypes.c_uint32*11)(128,128,*[int(v) for v in values],700,row['size_class'],0x02000000,0,0)
                    self.assertEqual(proc(q,ctypes.byref(FineInput(grid,None))),leg['observed_obstruction'])
                    self.assertEqual(leg['fine_index'],leg['fine_count']-2 if leg['observed_obstruction'] else 0)

    def test_ordinary_budget_requests_match_complete_original_partial_buffers(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-unit-fine-budget-1.27.json').read_text())
        class FineInput(ctypes.Structure):
            _fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]
        for engine in self.engines:
            proc=engine.pathing_fine_request_words
            proc.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(FineInput),ctypes.POINTER(ctypes.c_uint32)]
            for row in frozen['cases']:
                walls={x:i for i,x in enumerate([12,22,32,42])}
                cells=(ctypes.c_uint8*(64*64))(*(2 if x in walls and (y<54 if (walls[x]+row['pattern'])%2==0 else y>9) else 0 for y in range(64) for x in range(64)))
                q=(ctypes.c_uint32*15)(64,64,4,4,47,43,row['budget'],row['size_class'],0x02000000,0,0,*row['source_bits'],*row['goal_bits'])
                out=(ctypes.c_uint32*(6+2*16386))()
                proc(q,ctypes.byref(FineInput(cells,None)),out)
                self.assertEqual(list(out[:6]),[row[k] for k in ('result','pops','nodes','fine_count','fine_index','observed_obstruction')])
                self.assertEqual(list(out[6:6+2*row['fine_count']]),row['fine_words'])
        capture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-pathing-position-1.27.json').read_text())
        rows=capture['rows']
        budgets=[r['budget'] for r in rows if r.get('event')=='search' and r.get('kind')=='fine']
        self.assertEqual(budgets,[700])

    def test_primary_clock_observer_requires_native_order_and_words(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-primary-clock-inputs-1.27.json').read_text())
        trajectory = json.loads((ROOT / 'tools/ghidra/fixtures/retail-primary-clock-trajectory-1.27.json').read_text())
        # Reconstruct the primary observer rows using frozen original clock words;
        # presentation records and their host-dependent timing are outside this contract.
        rows = []; state = [0, 0, bits(300), 4096]; owners = 0
        for tick in range(6001):
            rows.append(dict(event='clock-source-begin', source='direct', input=bits(.005),
                maximum=bits(299), before=[state], caller='0x36aba8'))
            if tick and tick % 6 == 0:
                rows.append(dict(event='clock-owner-begin', clock=[state], counter=1024 + owners))
                rows.append(dict(event='clock-owner-end', clock=[state], counter=1025 + owners))
                owners += 1
            after = trajectory['advances'][tick] + [4096] if tick < 6000 else [fixture['completion_time'], 0, bits(300), 4096]
            if tick < 6000:
                rows.append(dict(event='clock-advance-begin', domain=20, input=bits(.005), before=state, caller='0x4f7ed'))
                rows.append(dict(event='clock-advance-end', domain=20, after=after, output=1))
            rows.append(dict(event='clock-source-end', source='direct', after=[after]))
            state = after
        self.assertEqual(primary_digest(rows), fixture['primary_sha256'])
        for engine in self.engines:
            self.assertEqual(verify_primary(rows, engine, fixture)['owner_callbacks'], 1000)
            for event, field, value in [('clock-source-begin', 'input', bits(.03)),
                ('clock-source-begin', 'caller', '0x4c0d0'),
                ('clock-advance-begin', 'domain', 21), ('clock-advance-end', 'output', True),
                ('clock-owner-begin', 'counter', 1025)]:
                changed = copy.deepcopy(rows)
                next(r for r in changed if r['event'] == event)[field] = value
                adjusted = dict(fixture, primary_sha256=primary_digest(changed))
                with self.assertRaises(ValueError): verify_primary(changed, engine, adjusted)
            changed = copy.deepcopy(rows)
            next(r for r in changed if r['event'] == 'clock-advance-end')['after'][0] ^= 1
            with self.assertRaises(ValueError): verify_primary(changed, engine, dict(fixture, primary_sha256=primary_digest(changed)))
            with self.assertRaises(ValueError): verify_primary(rows[:-1], engine, dict(fixture, primary_sha256=primary_digest(rows[:-1])))

    def test_original_primary_clock_old_velocity_and_predicted_frames(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-primary-clock-trajectory-1.27.json').read_text())
        self.assertEqual([len(fixture[k]) for k in ('advances', 'steps', 'frames', 'controls')],
                         [6000, 1000, 300, 1944])
        for engine in self.engines:
            engine.pathing_clock_advance.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            engine.pathing_native_pose.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                state = [0, 0, bits(300)]
                for expected in fixture['advances']:
                    out = (ctypes.c_uint32 * 4)()
                    engine.pathing_clock_advance((ctypes.c_uint32 * 5)(*state, 4096, bits(.005)), out)
                    self.assertEqual(list(out)[:3], expected)
                    self.assertEqual(out[3], 0)
                    state = list(out)[:3]
                for case in fixture['controls']:
                    out = (ctypes.c_uint32 * 4)()
                    arg = (ctypes.c_uint32 * 5)(*case['input'])
                    engine.pathing_clock_advance(arg, out)
                    self.assertEqual(list(out), case['output'])
                    self.assertEqual(list(arg), case['input'])
                for row in fixture['steps']:
                    old = row['before']
                    elapsed = subtract(row['clock'][0], old[0])
                    velocity = [multiply(v, bits(32)) for v in old[4:6]]
                    out = (ctypes.c_uint32 * 4)()
                    engine.pathing_native_pose((ctypes.c_uint32 * 7)(*old[2:4], *velocity,
                        *fixture['origin'], elapsed), out)
                    self.assertEqual(list(out), row['output'][:4])
                for row in fixture['frames']:
                    old = row['state']
                    elapsed = subtract(row['clock'][0], old[0])
                    velocity = [multiply(v, bits(32)) for v in old[4:6]]
                    out = (ctypes.c_uint32 * 4)()
                    engine.pathing_native_pose((ctypes.c_uint32 * 7)(*old[2:4], *velocity,
                        *fixture['origin'], elapsed), out)
                    self.assertEqual(list(out)[2:4], row['output'][2:4])
                    self.assertEqual(old[2:4], row['output'][:2])

    def test_public_axis_write_matches_original_and_next_move_commit(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-axis-position-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 576)
        self.assertEqual({r['phase'] for r in fixture['cases']}, {0, 1, 2, 3})
        for engine in self.engines:
            engine.pathing_pose_write.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            engine.pathing_native_pose.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for row in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 9)(*row['input'])
                    output = (ctypes.c_uint32 * 4)()
                    engine.pathing_pose_write(inputs, output)
                    self.assertEqual(list(output), row['output'])
                    self.assertEqual(list(inputs), row['input'])
                    velocity = (ctypes.c_uint32 * 6)(*row['input'][2:4], bits(100), bits(.6), bits(100), row['facing'])
                    engine.pathing_velocity_world_commit(velocity)
                    self.assertEqual(list(velocity)[:2], row['next'][4:6])
                    self.assertEqual(velocity[5], row['next'][6])
                    point = (ctypes.c_uint32 * 7)(*row['output'][:2], *velocity[:2], *row['input'][4:6], bits(.1))
                    engine.pathing_native_pose(point, output)
                    self.assertEqual(list(output), row['next'][:4])

    def test_public_axis_observer_requires_exact_queries_writes_and_repeat(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-axis-position-1.27.json').read_text())
        for engine in self.engines:
            configure_arrival(engine)
            results = [verify_axis_position(rows, engine, fixture) for rows in fixture['captures']]
            for key in ('position_sha256', 'decision_sha256', 'velocity_sha256'):
                self.assertEqual(results[0][key], results[1][key])
            self.assertEqual(results[0]['position_commits'], 8)
            self.assertEqual(results[0]['position_query_cases'], 40)
            self.assertEqual(results[0]['exact_velocity_commits'], 167)
            rows = fixture['captures'][0]
            for event, field, index in [('position-query', 'output', 0), ('position-commit', 'after', 2),
                                       ('position-commit', 'after', 4), ('position-native', 'output', None)]:
                changed = copy.deepcopy(rows)
                row = next(r for r in changed if r.get('event') == event)
                if index is None: row[field] ^= 1
                else: row[field][index] ^= 1
                adjusted = dict(fixture, position_sha256=axis_position_digest(changed))
                with self.assertRaises(ValueError): verify_axis_position(changed, engine, adjusted)
            for event in ('position-commit', 'position-query', 'position-native'):
                changed = copy.deepcopy(rows)
                changed.remove(next(r for r in changed if r.get('event') == event))
                with self.assertRaises(ValueError): verify_axis_position(changed, engine, fixture)
            for mutate in ('actor', 'notify', 'stop', 'sample', 'options', 'case_order'):
                changed = copy.deepcopy(rows)
                if mutate == 'actor': next(r for r in changed if r.get('event') == 'position-native')['unit'] = '0x123'
                elif mutate == 'notify': next(r for r in changed if r.get('event') == 'position-commit')['notify'] = 0
                elif mutate == 'stop': changed.remove(next(r for r in changed if r.get('event') == 'marker' and 'axis_stop_accepted' in r.get('value', '')))
                elif mutate == 'sample': changed.remove(next(r for r in changed if r.get('event') == 'marker' and 'tick=50 label=sample' in r.get('value', '')))
                elif mutate == 'options': changed[0]['owned'] = False
                else: next(r for r in changed if r.get('event') == 'position-marker')['value'] = 'PATHPOSE case=idle_x_same'
                adjusted = dict(fixture, position_sha256=axis_position_digest(changed))
                with self.assertRaises(ValueError): verify_axis_position(changed, engine, adjusted)

    def test_public_forced_position_requires_stop_then_exact_placement(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-forced-position-1.27.json').read_text())
        rows = fixture['observations']
        for engine in self.engines:
            configure_arrival(engine)
            for repeat in range(2):
                result = verify_forced_position(rows, engine, fixture)
                self.assertEqual([result[k] for k in ('public_position_calls','position_queries','position_commits',
                                 'stop_integrations','retired_orders','exact_velocity_commits')],[28,60,4,4,4,99])
            for mutation in ('truncated','actor','stop_flag','velocity','task','group','old_pose','placement',
                             'notify','getter','sample','observer','source'):
                changed = copy.deepcopy(rows)
                if mutation == 'truncated': changed.pop()
                elif mutation == 'actor': next(r for r in changed if r.get('event')=='position-native')['unit']='0x123'
                elif mutation == 'stop_flag': next(r for r in changed if r.get('event')=='forced-position-stop-begin')['flags']=0
                elif mutation in ('velocity','task','group','old_pose'):
                    row=next(r for r in changed if r.get('event')=='forced-position-stop-end')
                    if mutation=='velocity': row['after']['pose'][4]=1
                    elif mutation=='old_pose': row['after']['pose'][2]^=1
                    else: row['after']['taskHead' if mutation=='task' else 'group']=[1,2]
                elif mutation in ('placement','notify'):
                    row=next(r for r in changed if r.get('event')=='position-commit')
                    if mutation=='placement': row['after'][2]^=1
                    else: row['notify']=0
                elif mutation=='getter': next(r for r in changed if r.get('event')=='position-native')['output']^=1
                elif mutation=='sample': changed.remove(next(r for r in changed if r.get('event')=='marker' and 'tick=100 label=sample' in r['value']))
                elif mutation=='observer': changed[0]['owned']=False
                else: changed[0]['source_sha256']['wc3_pathfinding.js']='0'*64
                adjusted=dict(fixture,position_sha256=forced_position_digest(changed))
                with self.subTest(mutation=mutation,opt=engine._name), self.assertRaises(ValueError):
                    verify_forced_position(changed,engine,adjusted)

    def test_public_stop_recovery_replays_bounded_search_and_rejects_changes(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-stop-recovery-1.27.json').read_text())
        rows=fixture['rows']
        for engine in self.engines:
            configure_arrival(engine)
            result=verify_stop_recovery(rows,engine,fixture)
            self.assertEqual([result[k] for k in ('recovery_calls','placement_searches','recovered_points','exhausted_searches','public_getters')],[8,3,1,2,24])
            for mutation in ('truncated','receiver','limit','mask','callback','result','endpoint','fine-commit','exhaustion','getter','source'):
                changed=copy.deepcopy(rows)
                if mutation=='truncated': changed.pop()
                elif mutation=='receiver': next(r for r in changed if r.get('event')=='stop-recovery-begin')['mover']='0xBAD'
                elif mutation in ('limit','mask','callback'):
                    r=next(r for r in changed if r.get('event')=='stop-recovery-begin')
                    if mutation=='callback': r[mutation]=None
                    else:r[mutation]^=1
                elif mutation=='result': next(r for r in changed if r.get('event')=='stop-recovery-end')['result']^=1
                elif mutation=='endpoint': next(r for r in changed if r.get('event')=='placement-search-end')['after'][0]^=1
                elif mutation=='fine-commit': next(r for r in changed if r.get('event')=='stop-recovery-end' and r['case']=='stop_recovery_30')['after'][2]^=1
                elif mutation=='exhaustion': next(r for r in changed if r.get('event')=='stop-recovery-end' and r['case']=='stop_recovery_50')['after'][2]^=1
                elif mutation=='getter': next(r for r in changed if r.get('event')=='position-native')['output']^=1
                elif mutation=='source': changed[0]['source_sha256']['wc3_pathfinding.js']='0'*64
                adjusted=dict(fixture,recovery_sha256=stop_recovery_digest(changed))
                with self.subTest(mutation=mutation,opt=engine._name),self.assertRaises(ValueError):
                    verify_stop_recovery(changed,engine,adjusted)

    def test_public_pathing_placement_replays_zero_masks_and_rejects_changes(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-pathing-position-1.27.json').read_text())
        rows=fixture['rows']
        for engine in self.engines:
            configure_arrival(engine)
            result=verify_placement(rows,engine,fixture,True)
            self.assertEqual([result[k] for k in ('placement_searches','public_position_calls','position_queries','position_commits')],[7,25,65,5])
            for mutation in ('truncated','receiver','category','zero-mask','endpoint','velocity','source'):
                changed=copy.deepcopy(rows)
                if mutation=='truncated': changed.pop()
                elif mutation=='receiver': next(r for r in changed if r.get('event')=='pathing-toggle')['handle']+=1
                elif mutation=='category': next(r for r in changed if r.get('event')=='movement-mask-publication' and r['queryMask']==0)['objectCategory']=0
                elif mutation=='zero-mask': next(r for r in changed if r.get('event')=='placement-search-end' and r['mask']==0)['mask']=0x02000002
                elif mutation=='endpoint': next(r for r in changed if r.get('event')=='placement-search-end' and r['mask']==0)['after'][0]^=1
                elif mutation=='velocity': next(r for r in changed if r.get('event')=='forced-position-stop-end')['after']['pose'][4]=1
                elif mutation=='source': changed[0]['source_sha256']['wc3_pathfinding.js']='0'*64
                adjusted=dict(fixture,placement_sha256=placement_digest(changed,True))
                with self.subTest(mutation=mutation,opt=engine._name),self.assertRaises(ValueError):
                    verify_placement(changed,engine,adjusted,True)

    def test_public_blocked_placement_replays_candidates_and_rejects_changes(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-placement-1.27.json').read_text())
        rows=fixture['observations']
        for engine in self.engines:
            configure_arrival(engine)
            for repeat in range(2):
                result=verify_placement(rows,engine,fixture)
                self.assertEqual([result[k] for k in ('placement_searches','public_position_calls','position_queries','position_commits')],[7,30,78,6])
            for mutation in ('truncated','terrain','policy','mask','radius','limit','callback','context','mode','result','candidate','endpoint','getter','velocity','source','samples'):
                changed=copy.deepcopy(rows)
                if mutation=='truncated':changed.pop()
                elif mutation=='terrain':next(r for r in changed if r.get('event')=='terrain-native')['x']+=32
                elif mutation=='getter':next(r for r in changed if r.get('event')=='position-native')['output']^=1
                elif mutation=='velocity':next(r for r in changed if r.get('event')=='forced-position-stop-end')['after']['pose'][4]=1
                elif mutation=='source':changed[0]['source_sha256']['wc3_pathfinding.js']='0'*64
                elif mutation=='samples':changed.remove(next(r for r in changed if r.get('event')=='marker' and 'tick=100 label=sample ' in r.get('value','')))
                else:
                    r=next(r for r in changed if r.get('event')=='placement-search-end')
                    if mutation=='candidate':r['visits'][0]['result']=1
                    elif mutation=='endpoint':r['after'][0]^=1
                    elif mutation=='callback':r['callback']=None
                    elif mutation=='mode':r['restoredMode']^=1
                    else:r[mutation]^=1
                adjusted=dict(fixture,placement_sha256=placement_digest(changed))
                with self.subTest(mutation=mutation,opt=engine._name),self.assertRaises(ValueError):
                    verify_placement(changed,engine,adjusted)

    def test_terrain_natives_match_complete_original_queries_and_writes(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-terrain-natives-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),1040)
        self.assertEqual(fixture['original_calls'],3130)
        self.assertTrue(fixture['unrelated_cells_preserved'])
        for engine in self.engines:
            engine.pathing_terrain_native.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
            for repeat in range(2):
                for case in fixture['cases']:
                    output=(ctypes.c_uint32*4)()
                    engine.pathing_terrain_native((ctypes.c_uint32*7)(*case['input']),output)
                    self.assertEqual(list(output),case['output'],(case['input'],engine._name,repeat))

    def test_point_placement_matches_original_ring_endpoints(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-point-placement-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),1152)
        for engine in self.engines:
            engine.pathing_fine_placement.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32)]
            for repeat in range(2):
                for case in fixture['cases']:
                    q=case['input']; width,height=q[:2]; terrain=case['terrain']
                    blocked={(12,12)} if terrain=='one' else {(x,y) for y in range(10,15) for x in range(10,15)} if terrain=='square' else {(x,y) for y in range(height) for x in range(width)} if terrain=='sealed' else set()
                    data=[(q[6]>>24) if (x,y) in blocked else 0 for y in range(height) for x in range(width)]
                    output=(ctypes.c_uint32*3)()
                    engine.pathing_fine_placement((ctypes.c_uint32*8)(*q),(ctypes.c_uint8*len(data))(*data),output)
                    self.assertEqual(list(output),case['output'],(q,terrain,engine._name,repeat))

    def test_native_pose_retains_original_words_and_world_publication_rounding(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-native-pose-1.27.json').read_text())
        self.assertEqual(len(fixture['sequences']), 18)
        self.assertEqual(sum(len(s['steps']) for s in fixture['sequences']), 288)
        self.assertEqual(sum(r['direct_world'] != r['output'][2:]
                             for s in fixture['sequences'] for r in s['steps']), 139)
        for engine in self.engines:
            engine.pathing_native_pose.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for seq in fixture['sequences']:
                    prior = seq['position']
                    for row in seq['steps']:
                        self.assertEqual(row['input'][:2], prior)
                        inputs = (ctypes.c_uint32 * 7)(*row['input'])
                        output = (ctypes.c_uint32 * 4)()
                        engine.pathing_native_pose(inputs, output)
                        self.assertEqual(list(output), row['output'], row['input'])
                        self.assertEqual(list(inputs), row['input'])
                        prior = row['output'][:2]

    def test_world_grid_boundaries_match_complete_original_edits_and_scalar_inverse(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-world-grid-boundaries-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 576)
        for engine in self.engines:
            engine.pathing_world_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 6)(*case['input'])
                    output = (ctypes.c_uint32 * 6)()
                    engine.pathing_world_grid(inputs, output)
                    self.assertEqual(list(output), case['output'], case['input'])
                    self.assertEqual(list(inputs), case['input'])

    def test_public_mixed_formation_uses_dll_parsed_spacing(self):
        import gzip
        fixture=ROOT/'tools/ghidra/fixtures/retail-formation-ranks-1.27.jsonl.gz'
        rows=[json.loads(l) for l in gzip.decompress(fixture.read_bytes()).splitlines()]
        layout=next(r for r in rows if r.get('event')=='formation-rank-layout')
        inputs=[6,layout['heading']];expected=[1]
        for before,after in zip(layout['before']['members'],layout['after']['members']):
            self.assertEqual(before['pose'][4:6],[0,0])
            radius=next(m['radius'] for r in rows if r.get('event')=='group-routing-radius' and r['group']==layout['before']['group'] for m in r['members'] if m['resolved']==before['mover'])
            inputs+=before['pose'][2:6]+[0,0,0,0,0x41000000,radius,(before['moverFlags']>>12)&15]
            expected+=after['row'][3:5]
        for engine in self.engines:
            engine.pathing_formation_retail.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
            source=(ctypes.c_uint32*len(inputs))(*inputs);output=(ctypes.c_uint32*len(expected))()
            engine.pathing_formation_retail(source,output)
            self.assertEqual(list(output),expected)
            self.assertEqual(list(source),inputs)

    def test_formation_matches_original_mixed_ranks_radii_sort_ties_and_clocks(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-formation-layout-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 865)
        for engine in self.engines:
            engine.pathing_formation.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * len(case['input']))(*case['input'])
                    output = (ctypes.c_uint32 * len(case['output']))()
                    engine.pathing_formation(inputs, output)
                    self.assertEqual(list(output), case['output'], case['name'])
                    self.assertEqual(list(inputs), case['input'])
            # Reject the unproved larger caller domain before touching member data.
            inputs = (ctypes.c_uint32 * 2)(13, 0)
            output = (ctypes.c_uint32 * 3)(99, 123, 456)
            engine.pathing_formation(inputs, output)
            self.assertEqual(list(output), [0, 123, 456])

    def test_arrival_matches_complete_original_predicate_words(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-arrival-predicate-1.27.json').read_text())
        for engine in self.engines:
            engine.pathing_arrival.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 7)(*case['input'])
                    output = (ctypes.c_uint32 * 4)()
                    engine.pathing_arrival(inputs, output)
                    self.assertEqual(list(output), case['output'], case['input'])
                    self.assertEqual(list(inputs), case['input'])

    def test_speed_limits_match_original_profile_clamp_and_disabled_gate(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-speed-limits-1.27.json').read_text())
        for engine in self.engines:
            engine.pathing_speed_limits.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 6)(*case['input'])
                    output = (ctypes.c_uint32 * 3)()
                    engine.pathing_speed_limits(inputs, output)
                    self.assertEqual(list(output), case['output'], case['input'])
                    self.assertEqual(list(inputs), case['input'])

    def test_flat_speed_bonus_matches_original_maximum_and_clamp_composition(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-flat-speed-bonus-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']), 126)
        for engine in self.engines:
            engine.pathing_speed_bonus.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 8)(*case['input'])
                    output = (ctypes.c_uint32 * 3)()
                    engine.pathing_speed_bonus(inputs, output)
                    self.assertEqual(list(output), case['output'])
                    self.assertEqual(list(inputs), case['input'])

    def test_item_speed_observer_rejects_wrong_maximum_identity_and_publication(self):
        scenes = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-item-speed-1.27.json').read_text())['scenes']
        for fixture in scenes.values():
            rows = [dict(event='metadata', sha256=fixture['binary_sha256'], source_sha256=fixture['source_sha256'],
                         owned=True, motionEvents=True, velocityEvents=True, taskEvents=True)]
            rows += copy.deepcopy(fixture['sample'])
            for tick, label in [(0, fixture['start_label']), (10, 'order_accepted'), *[(t, 'sample') for t in range(1, 301)], (300, 'complete')]:
                rows.append(dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=0 y=0 order=851986'))
            rows.append(dict(event='trace-end', installed=True, counts=fixture['counts']))
            for engine in self.engines:
                with patch('verify_wc3_item_speed.verify_motion', return_value={'passed': True}):
                    self.assertGreater(verify_item_speed(rows, engine, fixture)['cached_cap_commits'], 0)
                    for event, key in [('speed-flat-bonus', 'authored'), ('speed-flat-maximum', 'output'), ('speed-composition', 'base'), ('speed-native', 'output'), ('velocity-commit', 'before')]:
                        changed = copy.deepcopy(rows)
                        row = next(r for r in changed if r.get('event') == event)
                        if key == 'before': row[key][6] ^= 1
                        else: row[key] ^= 1
                        adjusted = dict(fixture, speed_sha256=item_speed_digest(changed))
                        with self.assertRaises(ValueError): verify_item_speed(changed, engine, adjusted)
                    changed = copy.deepcopy(rows)
                    attached = [r for r in changed if r.get('event') == 'speed-flat-bonus' and r['case'] == 'item_two']
                    attached[1]['ability'] = attached[0]['ability']
                    with self.assertRaises(ValueError): verify_item_speed(changed, engine, fixture)
                    for changed in (rows[:-1], rows + [dict(type='error')], [r for r in rows if r.get('event') != 'speed-flat-maximum']):
                        with self.assertRaises(ValueError): verify_item_speed(changed, engine, fixture)

    def test_speed_cap_transition_matches_original_clock_vector_and_occupancy(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-speed-cap-transition-1.27.json').read_text())
        for engine in self.engines:
            engine.pathing_speed_cap.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            engine.pathing_speed_cap_world.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
            for _ in range(2):
                for case in fixture['cases']:
                    inputs = (ctypes.c_uint32 * 13)(*case['input'])
                    output = (ctypes.c_uint32 * 10)()
                    engine.pathing_speed_cap(inputs, output)
                    self.assertEqual(list(output), case['output'], case['input'])
                    self.assertEqual(list(inputs), case['input'])
                    world = [multiply(case['input'][i], 0x42000000) for i in (4, 5, 11)]
                    expected = [multiply(case['output'][i], 0x42000000) for i in (4, 5)] + [case['output'][9]]
                    converted = (ctypes.c_uint32 * 3)()
                    engine.pathing_speed_cap_world((ctypes.c_uint32 * 3)(*world), converted)
                    self.assertEqual(list(converted), expected)

    def test_speed_drop_observer_rejects_wrong_clock_and_unconsumed_clamp(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-speed-drop-1.27.json').read_text())
        rows = [dict(event='metadata', sha256=fixture['binary_sha256'], source_sha256=fixture['source_sha256'],
                     owned=True, motionEvents=True, velocityEvents=True, taskEvents=True)]
        commit = iter(fixture['next_commits'])
        for r in fixture['sample']:
            rows.append(copy.deepcopy(r))
            if r['event'] == 'speed-cap-change': rows.append(copy.deepcopy(next(commit)))
        for tick, label in [(0, 'start_speed_drop'), (10, 'order_accepted'), *[(t, 'sample') for t in range(1, 301)], (300, 'complete')]:
            rows.append(dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=0 y=0 order=851986'))
        rows.append(dict(event='trace-end', installed=True, counts={'speed-native': 6, 'speed-publication': 2, 'speed-cap-change': 2}))
        for engine in self.engines:
            with patch('verify_wc3_speed_drop.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_speed_drop(rows, engine, fixture)['immediate_zero_elapsed_clamps'], 1)
                for event, key in [('speed-cap-change', 'after'), ('speed-cap-change', 'clock'), ('speed-publication', 'identity'), ('velocity-commit', 'before')]:
                    changed = copy.deepcopy(rows)
                    row = next(r for r in reversed(changed) if r.get('event') == event)
                    row[key][4 if key in ('after', 'before') else 0] ^= 1
                    adjusted = dict(fixture, speed_sha256=speed_drop_digest(changed))
                    with self.assertRaises(ValueError): verify_speed_drop(changed, engine, adjusted)
                for changed in (rows[:-1], [r for r in rows if r.get('event') != 'speed-cap-change'],
                                [*rows, dict(type='error')]):
                    with self.assertRaises(ValueError): verify_speed_drop(changed, engine, fixture)

    def test_public_speed_observer_rejects_damaged_producers_and_travel_cap(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-speed-inputs-1.27.json').read_text())
        rows = [dict(event='metadata', sha256=fixture['binary_sha256'], source_sha256=fixture['source_sha256'],
                     owned=True, motionEvents=True, velocityEvents=True, taskEvents=True), *copy.deepcopy(fixture['sample'])]
        index = next(i for i, r in enumerate(rows) if r.get('event') == 'speed-publication' and r.get('case') == 'foot_travel_high')
        rows.insert(index + 1, fixture['travel_commit'])
        for tick, label in [(0, 'start_speed_inputs'), (10, 'order_accepted'), *[(t, 'sample') for t in range(1, 301)], (300, 'complete')]:
            rows.append(dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=0 y=0 order=851986'))
        rows.append(dict(event='trace-end', installed=True, counts={'speed-native': 120, 'speed-publication': 26}))
        mutations = []
        changed = copy.deepcopy(rows); changed.pop(1); mutations.append(changed)
        changed = copy.deepcopy(rows); changed[-1]['counts']['speed-native'] -= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[index + 1]['before'][6] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[-2]['value'] = 'PATHTRACE tick=300 label=sample x=0 y=0 order=851986'; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[0]['source_sha256']['map'] = '0' * 64; mutations.append(changed)
        changed = copy.deepcopy(rows); changed.insert(1, dict(type='error')); mutations.append(changed)
        for engine in self.engines:
            with patch('verify_wc3_speed_inputs.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_speed_inputs(rows, engine, fixture)['travel_speed_changes'], 1)
                for changed in mutations:
                    with self.assertRaises(ValueError): verify_speed_inputs(changed, engine, fixture)
                # Independent semantic checks must survive a newly computed sequence digest.
                for key in ('bounds', 'output', 'identity', 'limit'):
                    changed = copy.deepcopy(rows)
                    row = next(r for r in changed if key in r and r.get(key) is not None)
                    if isinstance(row[key], list): row[key][0] ^= 1
                    else: row[key] ^= 1
                    adjusted = dict(fixture, speed_sha256=speed_digest(changed))
                    with self.assertRaises(ValueError): verify_speed_inputs(changed, engine, adjusted)

    def test_point_arrival_observer_rejects_missing_or_changed_contract(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-point-arrival-inputs-1.27.json').read_text())
        rows = [dict(event='metadata', sha256=fixture['binary_sha256'], owned=True,
                     source_sha256=fixture['source_sha256'], motionEvents=True, velocityEvents=True, taskEvents=True),
                fixture['input'], fixture['publication'], *fixture['sample'],
                dict(event='trace-end', installed=True, counts={'arrival-input': 1, 'arrival-range': 1,
                     'arrival-evaluation': 2, 'velocity-commit': 2})]
        mutations = []
        changed = copy.deepcopy(rows); changed.pop(3); mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1]['worldRange'] = 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['after'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3]['storedPosition'][0] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3]['flags'] = 0x10000; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[3], changed[4] = changed[4], changed[3]; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[5]['angle'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[6]['after'][4] = 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[0]['source_sha256']['map'] = '0' * 64; mutations.append(changed)
        changed = copy.deepcopy(rows); changed.insert(1, dict(type='error')); mutations.append(changed)
        changed = copy.deepcopy(rows); changed.pop(); mutations.append(changed)
        for engine in self.engines:
            configure_arrival(engine)
            # Motion replay is independently covered; isolate the added producer/pairing checks.
            with patch('verify_wc3_arrival_trace.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_arrival(rows, engine, fixture)['arrival_evaluations'], 2)
                for changed in mutations:
                    with self.assertRaises(ValueError):
                        verify_arrival(changed, engine, fixture)

    def power_bridge(self, engine, name, a, b=0):
        proc = getattr(engine, 'pathing_' + name)
        proc.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        proc.restype = None
        result = (ctypes.c_uint32 * 2)(0xabcdef01, 0xabcdef02)
        proc(a, b, result)
        return list(result)

    def test_heading_alias_observer_rejects_changed_or_missing_producer_evidence(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-heading-aliases-1.27.json').read_text())
        fixture['heading_cases'] = 1
        fixture['alias_sha256'] = fixture['sample_sha256']
        rows = [dict(event='metadata', source_sha256=fixture['source_sha256'],
                     sha256=fixture['target_sha256'], headingEvents=True), *fixture['sample'],
                dict(event='trace-end', counts={'heading-acos-alias': 1, 'heading-vector-alias': 1, 'heading-error': 1})]
        mutations = []
        changed = copy.deepcopy(rows); changed.pop(1); mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1], changed[2] = changed[2], changed[1]; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[1]['inputPointer'] = changed[1]['outputPointer']; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['length'] ^= 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[2]['sequence'] += 1; mutations.append(changed)
        changed = copy.deepcopy(rows); changed[0]['source_sha256']['map'] = '0' * 64; mutations.append(changed)
        for engine in self.engines:
            # Motion's existing tests cover its complete trace contract; isolate the added nested observer checks.
            with patch('verify_wc3_heading_aliases.verify_motion', return_value={'passed': True}):
                self.assertEqual(verify_heading_aliases(rows, engine, fixture)['alias_cases'], 1)
                for changed in mutations:
                    with self.assertRaises(ValueError):
                        verify_heading_aliases(changed, engine, fixture)

    def test_compiled_integers_match_wrapped_model_and_live_native_inputs(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-compiled-integer-inputs-1.27.json').read_text())
        rng = random.Random(0x925350)
        texts = [c['input'].lstrip('-') for c in fixture['cases']]
        for _ in range(2000):
            word = rng.getrandbits(260)
            texts.extend([str(word), '0' + format(word, 'o'), '$' + format(word, 'x'), '0x' + format(word, 'X')])
        for engine in self.engines:
            engine.pathing_integer_literal.argtypes = [ctypes.c_char_p]
            engine.pathing_integer_literal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_integer_literal(text.encode()), integer_literal_word(text), text)
        for case in fixture['cases']:
            self.assertEqual(integer_source_word(case['input']), case['input_word'])
            self.assertEqual(integer_float(case['input_word']), case['output'])

    def test_compiled_literals_match_live_inputs_and_independent_model(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-compiled-literal-inputs-1.27.json').read_text())
        rng = random.Random(0x925260)
        texts = [c['input'].lstrip('-') for c in fixture['cases']]
        texts += [str(rng.getrandbits(180)) + '.' + str(rng.getrandbits(240)) for _ in range(2000)]
        for engine in self.engines:
            engine.pathing_literal.argtypes = [ctypes.c_char_p]
            engine.pathing_literal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_literal(text.encode()), literal_word(text), text)
        for case in fixture['cases']:
            self.assertEqual(source_word(case['input']), case['input_word'])
            self.assertEqual(saturating_integer_word(case['input_word']), case['output'])
        self.assertNotEqual(literal_word('0.59999999999999998'), decimal_bits('0.59999999999999998', reciprocal_table()))

    def test_power_curves_and_compiler_optimization_agree(self):
        rng = random.Random(0x127190)
        words = [rng.getrandbits(32) for _ in range(3000)]
        words += [bits(v / 32) for v in range(-256, 257)]
        for engine in self.engines:
            for name, model in [('corelog', corelog), ('reducedlog', reducedlog), ('log', log), ('exp', exp)]:
                for word in words:
                    try:
                        expected = [1, model(word)]
                    except ValueError:
                        expected = [0, 0xabcdef02]
                    self.assertEqual(self.power_bridge(engine, name, word), expected, (name, hex(word)))

    def test_public_power_matches_live_words(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-power-inputs-1.27.json').read_text())
        for case in fixture['cases']:
            a, b = case['input_word']
            self.assertEqual(public_power(a, b), case['output'])
            for engine in self.engines:
                self.assertEqual(self.power_bridge(engine, 'public_power', a, b), [1, case['output']])

    def test_power_nontermination_does_not_invent_output(self):
        for engine in self.engines:
            for name, a, b in [('power', bits(2), bits(2147483648)),
                               ('public_power', bits(2), bits(2147483648)),
                               ('exp', bits(536870912), 0)]:
                self.assertEqual(self.power_bridge(engine, name, a, b), [0, 0xabcdef02])
            self.assertEqual(self.power_bridge(engine, 'power', bits(2), bits(3)), [1, bits(8)])

    def test_rounding_boundaries_and_raw_words_match_independent_models(self):
        rng = random.Random(0x70110)
        words = [rng.getrandbits(32) for _ in range(5000)] + [0, 0x80000000, 1, 0x80000001]
        for value in (-8388608, -2, -1.5, -1, -.5, 0, .5, 1, 1.5, 2, 8388608):
            words.extend((bits(value) + step) & 0xffffffff for step in range(-4, 5))
        for engine in self.engines:
            for name, model in [('floor', floor_word), ('ceil', ceil_word), ('round', round_word),
                                ('truncate', truncate_word)]:
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word), model(word), (name, hex(word)))
            self.assertEqual(engine.pathing_ceil(0), bits(1))
            self.assertEqual(engine.pathing_ceil(0x80000000), bits(1))
            self.assertEqual(engine.pathing_floor(0x80000000), 0)
            self.assertEqual(engine.pathing_round(bits(-.5)), 0)

    def test_integer_reference_and_compiler_optimization_agree(self):
        rng = random.Random(0x12717085)
        pairs = [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(5000)]
        pairs += [(bits(1), bits(-1)), (bits(1), 0x33800000), (0, 0x80000000)]
        for name, reference in (('add', add), ('subtract', subtract), ('multiply', multiply)):
            for a, b in pairs:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(a, b), reference(a, b), (name, hex(a), hex(b)))

    def test_paired_trig_matches_independent_raw_word_model(self):
        rng = random.Random(0x71340)
        angles = [rng.getrandbits(32) for _ in range(20000)]
        for angle in (0, 0x80000000, 1, 0x80000001, 0x3f490fdb, 0x3fc90fdb, 0x40490fdb, 0x40c90fdb):
            angles.extend((angle + offset) & 0xffffffff for offset in range(-2, 3))
        sines = sine_table()
        for engine in self.engines:
            proc = engine.pathing_sincos
            proc.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
            proc.restype = None
            for angle in angles:
                result = (ctypes.c_uint32 * 2)()
                proc(angle, result)
                self.assertEqual(list(result), [trig_bits(angle, False, sines), trig_bits(angle, True, sines)])

    def test_remaining_angle_helpers_match_integer_models_and_optimization(self):
        table = reciprocal_table()
        ordinary, near = acos_tables()
        sines = sine_table()
        rng = random.Random(0x705b0)
        words = [rng.getrandbits(32) for _ in range(20000)]
        words += [w | sign for sign in (0,0x80000000) for pivot in (0x3e8930a3,0x3f7e8000,0x3f800000)
                  for w in range(pivot-16,pivot+17)]
        models = [('asin',lambda w:asin_bits(w,ordinary,near)), ('atan',lambda w:atan_bits(w,table)),
                  ('tan',lambda w:divide(trig_bits(w,False,sines),trig_bits(w,True,sines),table)),
                  ('degrees_to_radians',lambda w:multiply(w,0x3c8efa35)),
                  ('radians_to_degrees',lambda w:multiply(w,0x42652ee1))]
        for engine in self.engines:
            for name, model in models:
                proc = getattr(engine,'pathing_'+name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word),model(word),(name,hex(word)))
            engine.pathing_atan2.argtypes = [ctypes.c_uint32,ctypes.c_uint32]
            engine.pathing_atan2.restype = ctypes.c_uint32
            for y,x in zip(words,words[1:]):
                self.assertEqual(engine.pathing_atan2(y,x),atan2_bits(y,x,table),(hex(y),hex(x)))

    def test_decimal_and_public_integer_conversion_match_independent_models(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-public-numeric-inputs-1.27.json').read_text())
        texts = [case['input'] for case in fixture['cases'] if case['native'] == 'S2R']
        rng = random.Random(0x70de0)
        for _ in range(2000):
            digits = ''.join(str(rng.randrange(10)) for _ in range(rng.randrange(1, 100)))
            pivot = rng.randrange(len(digits) + 1)
            texts.append(rng.choice(['', '+', '-']) + digits[:pivot] + '.' + digits[pivot:] + rng.choice(['', 'e2', ';tail', ' tail', '..7']))
        words = [rng.getrandbits(32) for _ in range(20000)]
        table = reciprocal_table()
        for engine in self.engines:
            engine.pathing_decimal.argtypes = [ctypes.c_char_p]
            engine.pathing_decimal.restype = ctypes.c_uint32
            for text in texts:
                self.assertEqual(engine.pathing_decimal(text.encode()), decimal_bits(text, table), text)
            for name, model in [('integer_float', integer_float), ('saturating_integer', saturating_integer_word)]:
                proc = getattr(engine, 'pathing_' + name)
                proc.argtypes = [ctypes.c_uint32]
                proc.restype = ctypes.c_uint32
                for word in words:
                    self.assertEqual(proc(word), model(word), (name, hex(word)))
            for case in fixture['cases']:
                if case['native'] == 'S2R':
                    self.assertEqual(engine.pathing_decimal(case['input'].encode()), case['output'], case['id'])

    def test_decimal_bytes_match_live_public_words_and_all_byte_domains(self):
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-public-byte-inputs-1.27.json').read_text())
        table = reciprocal_table()
        for engine in self.engines:
            engine.pathing_decimal.argtypes = [ctypes.c_char_p]
            engine.pathing_decimal.restype = ctypes.c_uint32
            for case in fixture['cases']:
                source = source_bytes(case)
                self.assertEqual(engine.pathing_decimal(source), case['output'], case['id'])
                self.assertEqual(decimal_bits(source.decode('latin1'), table), case['output'], case['id'])
            for byte in range(1, 256):
                for source in (bytes([byte]), b'12'+bytes([byte])+b'34', b'.5'+bytes([byte])+b'7', b'-'+bytes([byte])+b'0.2'):
                    self.assertEqual(engine.pathing_decimal(source), decimal_bits(source.decode('latin1'), table), source.hex())

    def test_live_open_decision_and_window_boundary(self):
        # Frozen first open-ground Frida decision; the two nonzero significands truncate during addition.
        live = [1083572252, 1070141402, 0, 1083572224, 1058642330, 1065749137, 0]
        for engine in self.engines:
            words = (ctypes.c_uint32 * 7)(*live)
            engine.pathing_motion(words)
            self.assertEqual(list(words)[:2], [1091960846, 1070141402])
            for error, stopped in ((bits(0.5) - 1, False), (bits(0.5), True), (bits(0.5) + 1, True)):
                words = (ctypes.c_uint32 * 7)(bits(1), 0, error, bits(0.125), bits(0.125), bits(0.5), 0)
                engine.pathing_motion(words)
                self.assertEqual(words[0], 0 if stopped else bits(1.125))
                self.assertEqual(words[1], bits(0.125))

    def test_fraction_remainder_and_committed_facing(self):
        rng=random.Random(0x70fe0)
        table=reciprocal_table()
        pairs=[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(5000)]
        for engine in self.engines:
            for a,b in pairs:
                self.assertEqual(engine.pathing_fractional(a),fractional(a))
                self.assertEqual(engine.pathing_modulo(a,b),modulo(a,b,table),(hex(a),hex(b)))
            self.assertEqual(engine.pathing_facing_angle(0),0)
            self.assertEqual(engine.pathing_facing_angle(0x80000000),0x80000000)
            self.assertEqual(engine.pathing_facing_angle(0x40c90fdb),0x35490fdb)
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-committed-facing-1.27.json').read_text())
        for heading,expected in fixture['angles']:
            for engine in self.engines:self.assertEqual(engine.pathing_facing_angle(heading),expected)
        for x,y,maximum,before,after in fixture['cases']:
            for engine in self.engines:
                self.assertEqual(engine.pathing_velocity_heading(x,y,before),after)

    def test_full_turn_and_signed_zero_retain_retail_quirks(self):
        for engine in self.engines:
            self.assertEqual(engine.pathing_angle(0xc0c90fdb), 0)
            self.assertEqual(engine.pathing_angle(0x40c90fdb), 0x40c90fdb)
            self.assertEqual(engine.pathing_angle(0), 0x80000000)

    def test_retail_trig_is_integer_interpolation(self):
        table = sine_table()
        rng = random.Random(0x71280)
        words = [bits(x) for x in (0, 0.125, 0.6, 1, 1.5707963267948966, -1.5707963267948966, 3.141592653589793)]
        words += [rng.getrandbits(32) for _ in range(5000)]
        for name, cosine in (('sin', False), ('cos', True)):
            for word in words:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(word), trig_bits(word, cosine, table),
                                     (name, hex(word)))

    def test_root_and_reciprocal_use_retail_significands(self):
        table = reciprocal_table()
        rng = random.Random(0x71570)
        words = [bits(x) for x in (0, -0.0, -1, 0.125, 0.6, 1, 2, 4096)]
        words += [rng.getrandbits(32) for _ in range(5000)]
        for name, model in (('sqrt', square_root), ('reciprocal', lambda w: reciprocal(w, table))):
            for word in words:
                for engine in self.engines:
                    self.assertEqual(getattr(engine, 'pathing_' + name)(word), model(word), (name, hex(word)))

    def test_inverse_trig_thresholds_and_heading_fixture(self):
        ordinary,near = acos_tables()
        rng = random.Random(0x6ffa0)
        words = [rng.getrandbits(32) for _ in range(5000)]
        words += [w | sign for sign in (0,0x80000000)
                  for pivot in (0x3f7e8000,0x3f800000) for w in range(pivot-16,pivot+17)]
        for word in words:
            for engine in self.engines:
                self.assertEqual(engine.pathing_acos(word), acos_bits(word,ordinary,near), hex(word))
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-heading-errors-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),441)
        for x,y,heading,error in fixture['cases']:
            for engine in self.engines:
                self.assertEqual(engine.pathing_heading_error(x,y,heading),error,(hex(x),hex(y),hex(heading)))

    def test_complete_live_turn_velocity_and_integration(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['commits']), 192)
        for row in fixture['commits']:
            speed, heading, before, clock, after = row[0], row[1], row[2:10], row[10:13], row[13:21]
            for engine in self.engines:
                words = (ctypes.c_uint32 * 5)(*before[4:6], speed, heading, before[6])
                engine.pathing_velocity(words)
                self.assertEqual(list(words)[:2], after[4:6])
                self.assertEqual(list(words)[2:], [speed, heading, before[6]])
                full = (ctypes.c_uint32 * 6)(*before[4:6],speed,heading,*before[6:8])
                engine.pathing_velocity_commit(full)
                self.assertEqual(list(full)[:2],after[4:6])
                self.assertEqual(full[5],after[7])
                words = (ctypes.c_uint32 * 11)(*before[2:6], *before[:2], *clock, 0, 0)
                engine.pathing_integrate(words)
                self.assertEqual(list(words)[:2], after[2:4])
                self.assertEqual(list(words)[4:6], after[:2])

    def test_world_velocity_adapter_matches_original_cutoffs_and_live_words(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-world-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),1040)
        self.assertTrue(any(c['input'][2]>0 and c['expected'][:2]==[0,0] for c in fixture['cases']))
        for row in fixture['cases']:
            native=row['native_expected']
            expected=[w if w & 0x7fffffff == 0 else multiply(w,0x42000000) for w in native[:2]]+[native[2]]
            # Keep the frozen arithmetic projection and independently check stored words.
            self.assertEqual([multiply(w,0x42000000) for w in native[:2]]+[native[2]],row['expected'])
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*row['input'])
                engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],expected,row['input'])
                self.assertEqual(list(words)[2:5],row['input'][2:5])
        live=json.loads((ROOT/'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        for row in live['commits']:
            speed,heading,before,after=row[0],row[1],row[2:10],row[13:21]
            inputs=[multiply(w,0x42000000) for w in before[4:6]]+[multiply(speed,0x42000000),heading,multiply(before[6],0x42000000),before[7]]
            expected=[multiply(w,0x42000000) for w in after[4:6]]+[after[7]]
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*inputs);engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],expected)

    def test_shared_wall_pair_owner_commits_match_production_world_adapter(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-wall-pair-velocity-1.27.json').read_text())
        self.assertEqual(len(fixture['cases']),46)
        self.assertEqual({r['role'] for r in fixture['cases']},{'first','second'})
        for row in fixture['cases']:
            for engine in self.engines:
                words=(ctypes.c_uint32*6)(*row['world_input'])
                engine.pathing_velocity_world_commit(words)
                self.assertEqual([words[0],words[1],words[5]],row['expected'])

    def test_trace_rejects_missing_truncated_and_mutated_decisions(self):
        rows = [dict(event='metadata', sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236', motionEvents=True),
                dict(event='motion-decision', mover='a', speed=bits(1), heading=0, error=0,
                     increment=bits(0.125), turn=bits(0.125), window=bits(0.5), stop=0,
                     nextSpeed=bits(1.125), nextHeading=0),
                dict(event='trace-end', installed=True, samples=0, counts={'motion-decision': 1})]
        self.assertEqual(verify(rows, self.engines[0], None)['exact_decisions'], 1)
        for key, value in (('nextSpeed', bits(1)), ('nextHeading', bits(0.125)), ('error', -1)):
            bad = [rows[0], {**rows[1], key: value}, rows[2]]
            with self.assertRaises(ValueError): verify(bad, self.engines[0], None)
        with self.assertRaises(ValueError): verify([rows[0], rows[2]], self.engines[0], None)
        rows[-1]['counts']['motion-decision'] = 2
        with self.assertRaises(ValueError): verify(rows, self.engines[0], None)

    def test_trace_rejects_missing_truncated_and_mutated_velocity(self):
        fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-turn-velocity-1.27.json').read_text())
        row = fixture['commits'][0]
        commit = dict(event='velocity-commit', mover='a', speed=row[0], heading=row[1],
                      before=row[2:10], clock=row[10:13], after=row[13:21])
        metadata = dict(event='metadata', sha256=fixture['binary_sha256'], motionEvents=True, velocityEvents=True)
        decision = dict(event='motion-decision', mover='a', speed=bits(1), heading=0, error=0,
                        increment=bits(0.125), turn=bits(0.125), window=bits(0.5), stop=0,
                        nextSpeed=bits(1.125), nextHeading=0)
        ending = dict(event='trace-end', installed=True, samples=0,
                      counts={'motion-decision': 1, 'velocity-commit': 1})
        self.assertEqual(verify([metadata, decision, commit, ending], self.engines[0], None)['exact_velocity_commits'], 1)
        with self.assertRaises(ValueError): verify([metadata, decision, ending], self.engines[0], None)
        bad_end = {**ending, 'counts': {**ending['counts'], 'velocity-commit': 2}}
        with self.assertRaises(ValueError): verify([metadata, decision, commit, bad_end], self.engines[0], None)
        for key, value in (('speed', -1), ('before', []), ('clock', [True, 0, 0])):
            with self.assertRaises(ValueError):
                verify([metadata, decision, {**commit, key: value}, ending], self.engines[0], None)
        for index in (0, 2, 4, 6, 7):
            mutated = list(commit['after'])
            mutated[index] ^= 1
            with self.assertRaises(ValueError):
                verify([metadata, decision, {**commit, 'after': mutated}, ending], self.engines[0], None)

    def test_heading_capture_rejects_incomplete_or_changed_results(self):
        x,y,current,error = json.loads((ROOT/'tools/ghidra/fixtures/retail-heading-errors-1.27.json').read_text())['cases'][27]
        metadata = dict(event='metadata',sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',motionEvents=True,headingEvents=True)
        decision = dict(event='motion-decision',mover='a',speed=bits(1),heading=0,error=0,
                        increment=0,turn=bits(.5),window=bits(.5),stop=0,nextSpeed=bits(1),nextHeading=0)
        heading = dict(event='heading-error',vector=[x,y],heading=current,error=error)
        ending = dict(event='trace-end',installed=True,samples=0,counts={'motion-decision':1,'heading-error':1})
        self.assertEqual(verify([metadata,decision,heading,ending],self.engines[0],None)['exact_heading_errors'],1)
        with self.assertRaises(ValueError):verify([metadata,decision,ending],self.engines[0],None)
        for changed in ({'vector':[]},{'heading':True},{'error':error^1}):
            with self.assertRaises(ValueError):verify([metadata,decision,{**heading,**changed},ending],self.engines[0],None)
        with self.assertRaises(ValueError):verify([metadata,decision,heading,{**ending,'counts':{'motion-decision':1,'heading-error':2}}],self.engines[0],None)

if __name__ == '__main__':
    unittest.main()
