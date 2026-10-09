#!/usr/bin/env python3
"""Evidence checks run without retail assets, Wine, or Frida."""
import importlib.util
import copy
import json
import struct
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('path_trace', Path(__file__).resolve().parents[1] / 'tools/frida/analyze_pathfinding_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
numeric_spec = importlib.util.spec_from_file_location('numeric_inputs', Path(__file__).resolve().parents[1] / 'tools/frida/verify_wc3_numeric_inputs.py')
numeric = importlib.util.module_from_spec(numeric_spec)
numeric_spec.loader.exec_module(numeric)

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/frida'))
from verify_wc3_integer_inputs import verify as verify_integers, FIXTURE as INTEGER_FIXTURE
from verify_wc3_literal_inputs import verify as verify_literals, FIXTURE as LITERAL_FIXTURE
from verify_wc3_byte_inputs import verify as verify_bytes, FIXTURE as BYTE_FIXTURE, source_bytes, expected_digits
from make_wc3_pathfinding_map import byte_unit_name

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def capture(*searches, calls=None):
    counts = calls or {kind + '-search': sum(row['kind'] == kind for row in searches) for kind in ('acc', 'fine')}
    return [{'event': 'metadata', 'sha256': HASH}, *searches,
            {'event': 'trace-end', 'installed': True, 'samples': len(searches), 'counts': counts}]


class PathTraceTests(unittest.TestCase):
    def test_pathing_toggle_rejects_lost_brackets_masks_identity_and_wall_crossing(self):
        rows=[]; markers=[]
        for tick,flag,name in ((10,0,'disable'),(20,1,'enable'),(30,0,'disable'),(40,1,'enable')):
            rows.extend([dict(event='pathing-toggle',handle=123,enabled=flag,phase='enter'),
                dict(event='movement-mask-publication',rawcode=1751543663,category=202,queryMask=flag*2,
                     objectCategory=0x010000ca,pathMask=flag*0x02000002,mover='0x1234',identity=[17,19]),
                dict(event='pathing-toggle',handle=123,enabled=flag,phase='leave')])
            markers.extend(dict(tick=tick,label='pathing_'+name+'_'+phase,x=-1936,y=-600,order=851986)
                           for phase in ('before','after'))
        markers.append(dict(tick=38,label='sample',x=-1936,y=-550,order=851986))
        violations=[]; trace.check_pathing_toggle(rows,markers,violations); self.assertEqual(violations,[])
        for mode in ('bracket','category','query','path-mask','identity','receiver','script','crossing','order'):
            changed=copy.deepcopy(rows); samples=copy.deepcopy(markers)
            if mode=='bracket': changed.pop(2)
            elif mode=='category': changed[1]['objectCategory']=0
            elif mode=='query': changed[1]['queryMask']=2
            elif mode=='path-mask': changed[4]['pathMask']=2
            elif mode=='identity': changed[4]['identity']=[17,20]
            elif mode=='receiver': changed[3]['handle']=124
            elif mode=='script': samples.pop(1)
            elif mode=='crossing': samples[-1]['x']=-1872
            elif mode=='order': samples[3]['order']=0
            violations=[]; trace.check_pathing_toggle(changed,samples,violations)
            self.assertTrue(violations,mode)

    def test_byte_capture_rejects_substituted_producers_locales_and_lost_brackets(self):
        fixture = json.loads(BYTE_FIXTURE.read_text())
        rows = [dict(event='metadata', sha256=fixture['target_sha256'], source_sha256=fixture['source_sha256'],
                     crt=fixture['crt_config'], byteEvents=True, numericEvents=True), copy.deepcopy(fixture['crt_module'])]
        for case in fixture['cases']:
            identity = case['id']
            rows.append(dict(event='numeric-marker', value=f'PATHNUM case={identity} native=S2R'))
            rows.extend(expected_digits(case))
            rows.extend([dict(event='numeric-byte-parser', case=identity, native='S2R', text_hex=source_bytes(case).hex(), output=case['output']),
                         dict(event='numeric-native', case=identity, native='S2R', output=case['output']),
                         dict(event='numeric-marker', value=f'PATHNUM done={identity} value=completion')])
        destination = [struct.unpack('<f', struct.pack('<I', w))[0] for w in fixture['move_destination_words']]
        rows += [dict(event='point-task', destination=destination),
                 dict(event='marker', value='PATHTRACE tick=10 label=order_accepted x=0 y=0 order=851986'),
                 dict(event='marker', value='PATHTRACE tick=300 label=complete x=0 y=0 order=0'),
                 dict(event='trace-end', installed=True, counts={'numeric-native':514, 'numeric-parser':514, 'numeric-digit':1040})]
        self.assertEqual(verify_bytes(rows, fixture)['violations'], [])
        for mode in ('producer', 'signed', 'mask', 'table', 'locale', 'crt', 'output', 'bracket', 'count', 'incomplete', 'malformed'):
            changed = copy.deepcopy(rows)
            digit = next(r for r in changed if r.get('event') == 'numeric-digit')
            parsed = next(r for r in changed if r.get('event') == 'numeric-byte-parser')
            if mode == 'producer': parsed['text_hex'] = '54'  # literal TRIGSTR reference, not high byte
            elif mode == 'signed': digit['input'] = 128
            elif mode == 'mask': digit['output'] = 1
            elif mode == 'table': digit['table_word'] = 4
            elif mode == 'locale': changed[1]['locale_ever_changed'] = 1
            elif mode == 'crt': changed[0]['crt']['imageSize'] ^= 1
            elif mode == 'output': parsed['output'] ^= 1
            elif mode == 'bracket': changed.insert(-1, changed.pop(3))
            elif mode == 'count': changed[-1]['counts']['numeric-digit'] -= 1
            elif mode == 'incomplete': changed.pop()
            elif mode == 'malformed': parsed['text_hex'] = 'nothex'
            self.assertTrue(verify_bytes(changed, fixture)['violations'], mode)

    def test_byte_map_preserves_original_modifications_and_custom_objects(self):
        original = b'hbar'+b'\0'*4+struct.pack('<I',1)+b'unam'+struct.pack('<I',3)+b'Barracks\0'+b'\0'*4
        custom = struct.pack('<I',1)+b'hfoo'+b'xfoo'+struct.pack('<I',0)
        source = struct.pack('<II',1,1)+original+custom
        result = byte_unit_name(source)
        self.assertEqual(struct.unpack_from('<II',result),(1,2))
        self.assertEqual(result[8:8+len(original)],original)
        self.assertEqual(result[-len(custom):],custom)
        self.assertIn(b'unam'+struct.pack('<I',3)+bytes(range(128,256))+b'\0'*5,result)
        with self.assertRaisesRegex(ValueError,'already'): byte_unit_name(result)
        with self.assertRaisesRegex(ValueError,'version1'): byte_unit_name(struct.pack('<I',2)+source[4:])

    def test_integer_compiler_capture_rejects_radix_prefix_word_and_caller_changes(self):
        fixture = json.loads(INTEGER_FIXTURE.read_text())
        rows = [dict(event='metadata', sha256=fixture['target_sha256'], source_sha256=fixture['source_sha256'])]
        rows += copy.deepcopy(fixture['integer_sequence'])
        for case in fixture['cases']:
            identity, name = case['id'], case['native']
            rows.extend([dict(event='numeric-marker', value=f'PATHNUM case={identity} native={name}'),
                         dict(event='numeric-native', case=identity, native=name, input=case['input_word'], output=case['output']),
                         dict(event='numeric-marker', value=f'PATHNUM done={identity} value=completion')])
        destination = [struct.unpack('<f', struct.pack('<I', w))[0] for w in fixture['move_destination_words']]
        rows += [dict(event='point-task', destination=destination),
                 dict(event='marker', value='PATHTRACE tick=10 label=order_accepted x=0 y=0 order=851986'),
                 dict(event='marker', value='PATHTRACE tick=300 label=complete x=0 y=0 order=0'),
                 dict(event='trace-end', installed=True, counts={'numeric-native':44, 'numeric-integer-literal':42})]
        self.assertEqual(verify_integers(rows, fixture)['violations'], [])
        for mode in ('output', 'text', 'caller', 'token', 'radix', 'prefix', 'count', 'missing'):
            changed = copy.deepcopy(rows)
            if mode == 'count': changed[-1]['counts']['numeric-integer-literal'] -= 1
            elif mode == 'missing': changed.pop(1)
            elif mode == 'text': changed[1]['text'] = '1'
            else: changed[1][mode] ^= 1
            self.assertTrue(verify_integers(changed, fixture)['violations'], mode)

    def test_compiler_capture_rejects_words_callers_tokens_order_and_counts(self):
        fixture = json.loads(LITERAL_FIXTURE.read_text())
        rows = [dict(event='metadata', sha256=fixture['target_sha256'], source_sha256=fixture['source_sha256'])]
        rows += copy.deepcopy(fixture['literal_sequence'])
        for case in fixture['cases']:
            identity, name = case['id'], case['native']
            rows.extend([dict(event='numeric-marker', value=f'PATHNUM case={identity} native={name}'),
                         dict(event='numeric-native', case=identity, native=name, input=case['input_word'], output=case['output']),
                         dict(event='numeric-marker', value=f'PATHNUM done={identity} value=completion')])
        destination = [struct.unpack('<f', struct.pack('<I', w))[0] for w in fixture['move_destination_words']]
        rows += [dict(event='point-task', destination=destination),
                 dict(event='marker', value='PATHTRACE tick=10 label=order_accepted x=0 y=0 order=851986'),
                 dict(event='marker', value='PATHTRACE tick=300 label=complete x=0 y=0 order=0'),
                 dict(event='trace-end', installed=True, counts={'numeric-native':32, 'numeric-literal':40})]
        self.assertEqual(verify_literals(rows, fixture)['violations'], [])
        for mode in ('output', 'text', 'caller', 'token', 'order', 'count', 'missing'):
            changed = copy.deepcopy(rows)
            if mode == 'order': changed[1], changed[3] = changed[3], changed[1]
            elif mode == 'count': changed[-1]['counts']['numeric-literal'] -= 1
            elif mode == 'missing': changed.pop(1)
            elif mode == 'text': changed[1]['text'] = '0.6'
            else: changed[1][mode] ^= 1
            self.assertTrue(verify_literals(changed, fixture)['violations'], mode)

    def test_angle_capture_keeps_binary_operands_and_sparse_zero_parser_count(self):
        fixture = json.loads(numeric.FIXTURE.with_name('retail-public-angle-inputs-1.27.json').read_text())
        rows = [dict(event='metadata',sha256=fixture['target_sha256'],source_sha256=fixture['source_sha256'])]
        for case in fixture['cases']:
            identity, name = case['id'],case['native']
            rows.extend([dict(event='numeric-marker',value=f'PATHNUM case={identity} native={name}'),
                         dict(event='numeric-native',case=identity,native=name,input=case['input_word'],output=case['output']),
                         dict(event='numeric-marker',value=f'PATHNUM done={identity} value=completion')])
        rows.extend([dict(event='point-task',destination=[-1936.0,-144.0]),
                     dict(event='marker',value='PATHTRACE tick=10 label=order_accepted x=0 y=0 order=851986'),
                     dict(event='marker',value='PATHTRACE tick=300 label=complete x=0 y=0 order=0'),
                     dict(event='trace-end',installed=True,counts={'numeric-native':48})])
        self.assertEqual(numeric.verify(rows,fixture)['violations'],[])
        changed = copy.deepcopy(rows)
        row = next(r for r in changed if r.get('case') == 'atan2_10' and r['event'] == 'numeric-native')
        row['input'].reverse()
        self.assertIn('bracketed parser/native sequence or exact words differ',numeric.verify(changed,fixture)['violations'])
        changed = copy.deepcopy(rows)
        changed[-1]['counts']['numeric-parser'] = 1
        self.assertIn('observer numeric counts differ from recorded events',numeric.verify(changed,fixture)['violations'])

    def test_public_numeric_contract_rejects_word_order_and_provenance_changes(self):
        fixture = json.loads(numeric.FIXTURE.read_text())
        rows = [dict(event='metadata', sha256=fixture['target_sha256'], source_sha256=fixture['source_sha256'])]
        for case in fixture['cases']:
            identity, name = case['id'], case['native']
            rows.append(dict(event='numeric-marker', value=f'PATHNUM case={identity} native={name}'))
            if name == 'S2R' and case['input']:
                rows.append(dict(event='numeric-parser', case=identity, native=name, text=case['input'], output=case['output']))
            row = dict(event='numeric-native', case=identity, native=name, output=case['output'])
            if 'input_word' in case: row['input'] = case['input_word']
            rows.append(row)
            rows.append(dict(event='numeric-marker', value=f'PATHNUM done={identity} value=completion'))
        destination = [struct.unpack('<f', struct.pack('<I', w))[0] for w in fixture['move_destination_words']]
        rows += [dict(event='point-task', destination=destination),
                 dict(event='marker', value='PATHTRACE tick=10 label=order_accepted x=0 y=0 order=851986'),
                 dict(event='marker', value='PATHTRACE tick=300 label=complete x=0 y=0 order=0'),
                 dict(event='trace-end', installed=True, counts={'numeric-native':67, 'numeric-parser':25})]
        self.assertEqual(numeric.verify(rows, fixture)['violations'], [])
        for mode in ['output','input','text','bracket','provenance','destination','count','failure','missing']:
            changed = copy.deepcopy(rows)
            if mode == 'output': next(r for r in changed if r['event'] == 'numeric-native')['output'] ^= 1
            elif mode == 'input': next(r for r in changed if 'input' in r)['input'] ^= 1
            elif mode == 'text': next(r for r in changed if r['event'] == 'numeric-parser')['text'] = '1.250001'
            elif mode == 'bracket': changed[1], changed[2] = changed[2], changed[1]
            elif mode == 'provenance': changed[0]['source_sha256']['map'] = '0' * 64
            elif mode == 'destination': next(r for r in changed if r['event'] == 'point-task')['destination'][0] = -1936.25
            elif mode == 'count': changed[-1]['counts']['numeric-native'] -= 1
            elif mode == 'failure': changed.append(dict(type='error', event='error'))
            else: changed.pop(2)
            self.assertTrue(numeric.verify(changed, fixture)['violations'], mode)

    def test_patrol_reversal_requires_approach_and_return_under_current_head(self):
        markers = [dict(tick=t, label='sample', x=-1936.0,
                        y=-464.0 + 14 * min(t - 150, 44 - (t - 150)), order=851991)
                   for t in range(150, 180)]
        errors = []
        trace.check_patrol_reversal(markers, errors)
        self.assertEqual(errors, [])
        for mode in ('stationary', 'outbound_only', 'wrong_head', 'truncated'):
            changed = [dict(m) for m in markers]
            if mode == 'stationary':
                for m in changed: m['y'] = -464.0
            elif mode == 'outbound_only':
                for m in changed: m['y'] = -464.0 + 10 * (m['tick'] - 150)
            elif mode == 'wrong_head':
                changed[-1]['order'] = 0
            else:
                changed.pop()
            errors = []
            trace.check_patrol_reversal(changed, errors)
            self.assertIn('Patrol lacks endpoint approach and return under its current head', errors)

    def test_order_lifecycle_rejects_historical_or_animation_derived_heads(self):
        changes = [(0, 'start_order_lifecycle', 0), (10, 'point_move_accepted', 851986),
                   (30, 'hold_accepted', 0), (60, 'defend_accepted', 0), (65, 'undefend_accepted', 0),
                   (80, 'target_smart_accepted', 851971), (110, 'target_near', 851971),
                   (120, 'target_move_accepted', 851986), (130, 'target_removed', 0),
                   (140, 'stop_accepted', 0), (150, 'patrol_accepted', 851991),
                   (170, 'invalid_rejected', 851991), (180, 'hold_again_accepted', 0),
                   (200, 'enemy_near', 0), (230, 'follow_before_death_accepted', 851986),
                   (240, 'killed', 0), (250, 'dead_move_rejected', 0), (300, 'complete', 0)]
        markers = [dict(tick=t, label=label, order=order) for t, label, order in changes]
        markers += [dict(tick=t, label='sample', order=next(o for start, _, o in reversed(changes) if start <= t))
                    for t in range(1, 301)]
        errors = []
        self.assertEqual(trace.check_order_lifecycle(markers, errors), changes)
        self.assertEqual(errors, [])
        for tick, invalid_head in [(31, 851993), (61, 852055), (119, 0), (131, 851986), (171, 0), (231, 0), (241, 851986)]:
            changed = [dict(m) for m in markers]
            next(m for m in changed if m['label'] == 'sample' and m['tick'] == tick)['order'] = invalid_head
            errors = []
            trace.check_order_lifecycle(changed, errors)
            self.assertIn('public order lifecycle sampled head differs from fixture', errors)
        errors = []
        trace.check_order_lifecycle(markers[1:], errors)
        self.assertIn('public order lifecycle transitions differ from fixture', errors)
        rows = capture()
        for m in markers:
            m['x'], m['y'] = -1936.0, -464.0
            if 150 <= m['tick'] < 180:
                t = m['tick'] - 150
                m['y'] += 14 * min(t, 44 - t)
        rows[1:1] = [dict(event='marker', value=f"PATHTRACE tick={m['tick']} label={m['label']} x={m['x']} y={m['y']} order={m['order']}") for m in markers]
        rows[1:1] = [dict(event='hold-marker', value='PATHHOLD tick=200 life=420.000'),
                     dict(event='hold-marker', value='PATHHOLD tick=220 life=409.606')]
        self.assertEqual(trace.analyze(rows, 'order_lifecycle')['violations'], [])
        errors = []
        self.assertTrue(trace.compare_order_lifecycle(rows, rows, errors)['equal'])
        self.assertEqual(errors, [])
        changed = [dict(row) for row in rows]
        changed[2]['value'] = 'PATHHOLD tick=220 life=409.000'
        errors = []
        self.assertFalse(trace.compare_order_lifecycle(rows, changed, errors)['equal'])
        self.assertIn('order lifecycle repeat metadata or timer markers differ', errors)
        rows[2]['value'] = 'PATHHOLD tick=220 life=420.000'
        self.assertIn('Hold lacks observed automatic damage while current head is retired',
                      trace.analyze(rows, 'order_lifecycle')['violations'])

    def test_widget_scenario_checks_lifecycle_without_terrain_native_edits(self):
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=0 y=0 order=0')
        rows = capture()
        rows[1:1] = [marker(0, 'start_widget_lifecycle'), marker(0, 'before_widget_create'),
                     marker(0, 'after_widget_create'), marker(10, 'order_accepted'),
                     marker(50, 'before_widget_remove'), marker(50, 'after_widget_remove'),
                     *[marker(t, 'sample') for t in range(1, 301)], marker(300, 'complete')]
        self.assertEqual(trace.analyze(rows, 'widget_lifecycle')['violations'], [])
        del rows[3]
        self.assertIn('scenario obstacle transition markers missing or duplicated',
                      trace.analyze(rows, 'widget_lifecycle')['violations'])

    def test_widget_pair_requires_same_widget_collection_and_order(self):
        create = dict(event='widget-method', method='create', widget='a', beforeCollection='0x0', afterCollection='c')
        destroy = dict(event='widget-method', method='destroy', widget='a', beforeCollection='c', afterCollection='0x0')
        errors = []
        self.assertEqual(len(trace.summarize_widgets([create, destroy], errors)['paired_create_destroy']), 1)
        self.assertEqual(trace.summarize_widgets([destroy, create], errors)['paired_create_destroy'], [])
        self.assertEqual(len(trace.summarize_widgets([create, destroy, destroy], errors)['paired_create_destroy']), 1)
        for change in (dict(widget='b'), dict(beforeCollection='d'), dict(afterCollection='c')):
            self.assertEqual(trace.summarize_widgets([create, {**destroy, **change}], errors)['paired_create_destroy'], [])
        self.assertEqual(errors, [])

    def test_widget_sample_counts_cannot_silently_drop_returns(self):
        rows = [dict(event='metadata', widgetEvents=True, samples=1),
                dict(event='trace-end', counts={'widget-create': 3})]
        errors = []
        trace.summarize_widgets(rows, errors)
        self.assertEqual(errors, ['widget sample/count mismatch: create'])
        rows.insert(1, dict(event='widget-method', method='create'))
        errors = []
        trace.summarize_widgets(rows, errors)
        self.assertEqual(errors, [])

    def test_task_records_match_bounded_final_counters(self):
        rows=[dict(event='metadata',taskEvents=True,samples=2),
              dict(event='task-arrival'),dict(event='task-arrival'),
              dict(event='trace-end',counts={'task-arrival':3})]
        errors=[]
        trace.summarize_tasks(rows,errors)
        self.assertEqual(errors,[])
        rows.pop(1)
        errors=[]
        trace.summarize_tasks(rows,errors)
        self.assertIn('task sample/count mismatch: task-arrival',errors)

    def test_task_prepend_links_prior_head(self):
        row=dict(event='task-prepend',taskIdentity=[3,103],afterHead=[3,103],
                 beforeHead=[2,102],successor=[2,102])
        errors=[]
        self.assertEqual(trace.summarize_tasks([row],errors)['prepends'],1)
        self.assertEqual(errors,[])
        row['successor']=[3,103]
        errors=[]
        trace.summarize_tasks([row],errors)
        self.assertIn('prepended task successor differs from prior head',errors)

    def test_point_task_acceptance_requires_dispatch_bit_clear(self):
        state = dict(ability='a', unit='u', abilityFlags=4, taskHead=[3,103], unitFlags=0)
        row = dict(event='point-task', before=state.copy(), after=state.copy(),
                   taskIdentity=[3,103], eventCode=0xd016c)
        errors=[]
        self.assertEqual(trace.summarize_tasks([row], errors)['accepted'],1)
        self.assertEqual(errors,[])
        row['after']['unitFlags']=1
        errors=[]
        trace.summarize_tasks([row],errors)
        self.assertIn('accepted point task retained dispatch bit',errors)
        row['after'].update(unitFlags=0,abilityFlags=0)
        errors=[]
        self.assertEqual(trace.summarize_tasks([row],errors)['accepted'],0)
        self.assertEqual(errors,[])

    def test_yield_countdown_preserves_disabled_path(self):
        setter = dict(event='yield-set', before=25, requested=4, after=25, stored=[1,2], identity=[1,2], caller='c')
        delay = dict(event='path-delay', path='p', before=25, after=25, disabled=True, result=0x100000, counter=10)
        errors=[]
        trace.summarize_yielding([setter,delay],errors)
        self.assertEqual(errors,[])
        delay.update(disabled=False,after=24,result=1)
        errors=[]
        trace.summarize_yielding([setter,delay],errors)
        self.assertEqual(errors,[])
        setter['after']=4
        errors=[]
        trace.summarize_yielding([setter,delay],errors)
        self.assertTrue(errors)

    def test_blocker_summary_checks_real_rejection_conditions(self):
        obj = dict(hits=3, objectMask=0x01000001, queryMask=1, flags=0, mode=0, isMover=True, payload='m')
        rows = [dict(event='route', kind='fine', blockers=dict(objectHits=3, terrainHits=2, boundsHits=0,
                    omittedHits=0, objects={'o': obj}))]
        errors = []
        self.assertEqual(trace.summarize_blockers(rows, errors)['movers'], ['m'])
        self.assertEqual(errors, [])
        obj['flags'] = 0x20000000
        errors = []
        trace.summarize_blockers(rows, errors)
        self.assertTrue(errors)
        obj['mode'] = 1
        errors = []
        trace.summarize_blockers(rows, errors)
        self.assertEqual(errors, [])
        obj['hits'] = 2
        errors = []
        trace.summarize_blockers(rows, errors)
        self.assertTrue(errors)

    def test_ground_crowd_requires_same_path_counter_retry_chain(self):
        rows, crowd = [], {'final': {}}
        for n in range(9):
            x, y = -2016 + n % 3 * 80, -1056 + n // 3 * 80
            rows.append(dict(event='arrival-transition', mover=str(n), source=[(x+7168)/32, (y+3072)/32], result=0))
            final_y = -240 if n == 0 else -144
            rows.append(dict(event='arrival-transition', mover=str(n), source=[163.5, (final_y+3072)/32],
                             result=1, flags=65536 if n == 0 else 0, threshold=0.49, ms=10))
            crowd['final'][str(n)] = [300, -1936, final_y, 0]
        force = dict(event='force-arrival', mover='0', path='p', counter=20, ms=5,
                     caller='0x16fd32', advance={'result': 4})
        retry = dict(event='retry-result', path='p', counter=20, before=1, after=1, result=4)
        rows.extend([force, retry])
        errors = []
        result = trace.check_ground_crowd_arrivals(rows, crowd, errors)
        self.assertEqual(errors, [])
        self.assertTrue(result[0]['retry_exhaustion_linked'])
        retry['counter'] = 19
        errors = []
        trace.check_ground_crowd_arrivals(rows, crowd, errors)
        self.assertTrue(errors)

    def test_separation_rejects_invalid_or_oversized_vectors(self):
        row = dict(event='separation-active', mover='m', selector=0, vector=[0, 0],
                   afterVector=[0.3, 0.4], position=[1, 2], afterPosition=[1, 2])
        rows = capture()
        rows.insert(1, row)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['afterVector'] = [0.5, 0.5]
        self.assertTrue(trace.analyze(rows)['violations'])
        row['afterVector'] = [float('nan'), 0]
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_crowd_requires_every_unit_tick_and_accepted_orders(self):
        rows = [{'event': 'crowd-marker', 'value': f'PATHCROWD tick={tick} id={unit} x=1.0 y=2.0 order=0'}
                for tick in range(1, 301) for unit in range(9)]
        rows += [{'event': 'crowd-marker', 'value': f'PATHCROWD tick=10 id={unit} ordered=1'}
                 for unit in range(9) if unit != 4]
        errors = []
        self.assertEqual(trace.check_crowd_experiment(rows, errors)['samples'], 2700)
        self.assertEqual(errors, [])
        errors = []
        trace.check_crowd_experiment(rows[1:], errors)
        self.assertTrue(errors)
        rows[-1]['value'] = 'PATHCROWD tick=10 id=8 ordered=0'
        errors = []
        trace.check_crowd_experiment(rows, errors)
        self.assertTrue(errors)

    def test_retry_initialization_and_terminal_count(self):
        rows = capture()
        init = dict(event='retry-init', path='p', count=2, thresholdSquared=144)
        first = dict(event='retry-result', path='p', before=0, after=1, result=1)
        last = dict(event='retry-result', path='p', before=1, after=1, result=4)
        rows[1:1] = [init, first, last]
        self.assertEqual(trace.analyze(rows)['violations'], [])
        self.assertEqual(trace.analyze(rows)['retry_exhaustions'], 1)
        last['after'] = 0
        self.assertTrue(trace.analyze(rows)['violations'])
        last['after'] = 1
        init['count'] = 7
        self.assertTrue(trace.analyze(rows)['violations'])
        first['after'] = 6
        self.assertEqual(trace.analyze(rows)['violations'], [])
        init['thresholdSquared'] = 12
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_target_perimeter_excludes_interior(self):
        hit = dict(event='target-perimeter-hit', center=[163, 89], offset=2, width=4, matched=[163, 90])
        rows = capture()
        rows.insert(1, hit)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        hit['matched'] = [163, 89]
        self.assertTrue(trace.analyze(rows)['violations'])
        hit['matched'] = [163, 91]
        self.assertTrue(trace.analyze(rows)['violations'])
        hit['matched'] = None
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_force_arrival_requires_current_route_result(self):
        row = dict(event='force-arrival', before=2, after=0x10002, caller='0x16fd32', path='p', counter=100,
                   advance={'path': 'p', 'counter': 100, 'result': 3})
        rows = capture()
        rows.insert(1, row)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['advance']['counter'] = 99
        self.assertTrue(trace.analyze(rows)['violations'])
        row['advance']['counter'] = 100
        row['advance']['result'] = 2
        self.assertTrue(trace.analyze(rows)['violations'])
        row['advance']['result'] = 4
        row['after'] = 0x10000
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_fog_reacquisition_requires_cached_goal_and_same_group(self):
        rows = [dict(event='group-target', group='g', path='p', target='t')]
        for tick in range(1, 301):
            rows.append(dict(event='target-marker', value=f'PATHTARGET tick={tick} x=-1936.000 y={-144 if tick < 70 else 112:.3f} visible={int(not 60 <= tick < 75)}'))
            rows.append(dict(event='marker', value=f'PATHTRACE tick={tick} label=sample x=-1936.000 y=-400.000 order=851971'))
            if tick == 60:
                rows.extend([dict(event='target-visibility', group='g', path='p', target='t', blocked=1, counter=100),
                             dict(event='arrival-transition', threshold=0.49000000953674316)])
            if tick == 70:
                rows.append(dict(event='group-completion', group='g', missed=33, flags=1, gateOpen=True, counter=132))
            if tick == 75:
                rows.extend([dict(event='target-visibility', group='g', path='p', target='t', blocked=0, counter=150),
                             dict(event='path-destination', caller='0x16ce73', path='p', destination=[163.5, 99.5]),
                             dict(event='group-completion', group='g', missed=0, flags=1, gateOpen=False, counter=150),
                             dict(event='arrival-transition', threshold=11.3125, previous={'threshold': 0.49000000953674316})])
        def check():
            errors = []
            trace.check_fog_experiment(rows, errors, True)
            return errors
        self.assertEqual(check(), [])
        restored = next(r for r in rows if r.get('event') == 'target-visibility' and r['blocked'] == 0)
        restored['group'] = 'other'
        self.assertTrue(check())
        restored['group'] = 'g'
        destination = next(r for r in rows if r.get('event') == 'path-destination')
        destination['destination'] = [163.5, 91.5]
        self.assertTrue(check())
        destination['destination'] = [163.5, 99.5]
        rows.insert(100, dict(event='target-lost-dispatch'))
        self.assertTrue(check())
        rows.pop(100)
        gate = next(r for r in rows if r.get('missed') == 33)
        gate['missed'] = 32
        self.assertTrue(check())

    def test_target_loss_requires_same_target_ability_and_ordered_stop(self):
        dispatch = dict(event='target-lost-dispatch', ms=100, caller='0x68b7d8', targetUnit='target')
        handler = dict(event='move-target-lost', ms=100, eventCode=0xd01a4, targetUnit='target', ownerUnit='owner', ability='move')
        check = dict(event='move-target-validation', ms=100, target='target', ability='move', result=0xdd)
        stop = dict(event='mover-stop', ms=100, caller='0x5ca8b', stack=['0x5ff645', '0x65108f'])
        rows = [dispatch, handler, check, stop]
        self.assertEqual(len(trace.target_loss_chains(rows)), 1)
        check['target'] = '0x0'
        self.assertEqual(trace.target_loss_chains(rows), [])
        check['target'] = 'target'
        check['ability'] = 'another'
        self.assertEqual(trace.target_loss_chains(rows), [])
        check['ability'] = 'move'
        self.assertEqual(trace.target_loss_chains([dispatch, handler, stop, check]), [])
        stop['ms'] = 200
        self.assertEqual(trace.target_loss_chains(rows), [])
        stop['ms'] = 100
        handler['eventCode'] = 0
        self.assertEqual(trace.target_loss_chains(rows), [])

    def test_visibility_loss_cancels_before_shift_and_does_not_resume(self):
        rows = [dict(event='group-target', group='g', path='p', target='t'),
                dict(event='target-visibility', group='g', path='p', target='t', blocked=0)]
        for tick in range(1, 301):
            if tick == 80:
                rows.append(dict(event='marker', value='PATHTRACE tick=80 label=before_hidden_shift x=-1936.000 y=-503.527 order=0'))
            rows.append(dict(event='target-marker', value=f'PATHTARGET tick={tick} x=-1936.000 y={-144 if tick < 80 else 112:.3f} visible={int(not 80 <= tick < 160)}'))
            rows.append(dict(event='marker', value=f'PATHTRACE tick={tick} label=sample x=-1936.000 y=-503.527 order={851971 if tick < 80 else 0}'))
        def check():
            violations = []
            trace.check_visibility_experiment(rows, violations)
            return violations
        self.assertEqual(check(), [])
        hidden = next(r for r in rows if r.get('event') == 'target-marker' and 'tick=100 ' in r['value'])
        hidden['value'] = hidden['value'].replace('visible=0', 'visible=1')
        self.assertTrue(check())
        hidden['value'] = hidden['value'].replace('visible=1', 'visible=0')
        before = next(r for r in rows if 'label=before_hidden_shift' in r.get('value', ''))
        before['value'] = before['value'].replace('order=0', 'order=851971')
        self.assertTrue(check())
        before['value'] = before['value'].replace('order=851971', 'order=0')
        final = rows[-1]
        final['value'] = final['value'].replace('order=0', 'order=851971')
        self.assertTrue(check())
        final['value'] = final['value'].replace('order=851971', 'order=0')
        rows[1]['target'] = 'unrelated'
        self.assertTrue(check())
        rows[1]['target'] = 't'
        rows.pop(2)
        self.assertTrue(check())

    def test_refresh_clamp_flag_and_lifetime_gap(self):
        first = dict(event='target-refresh', group='g', path='p', distanceFine=26,
                     coefficient=0.33000001311302185, unclamped=9, flags=0, reload=16, counter=100)
        second = {**first, 'counter': 117}
        rows = capture()
        rows[1:1] = [first, second]
        self.assertEqual(trace.analyze(rows)['violations'], [])
        self.assertEqual(trace.analyze(rows)['target_refresh']['counter_gap_histogram'], {17: 1})
        second['flags'] = 0x400
        self.assertTrue(trace.analyze(rows)['violations'])
        second['reload'] = 181
        self.assertEqual(trace.analyze(rows)['violations'], [])
        rows.insert(2, dict(event='group-target', group='g', path='p', target='0x0'))
        self.assertEqual(trace.analyze(rows)['target_refresh']['counter_gap_histogram'], {})

    def test_follow_walk_rejects_jump_and_unrelated_search(self):
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=-1936.000 y=-503.527 order=851971')
        search = dict(event='search', kind='acc', path='0x100', request=1, budget=5000,
                      nodes=1, pops=1, result=0, levels={'0': 1})
        later = {**search, 'request': 2}
        rows = capture(calls={'acc-search': 2})
        rows[-1]['samples'] = 2
        events = [marker(0, 'start_follow_walk'), marker(10, 'order_accepted'),
                  dict(event='group-target', path='0x100', target='0x200'),
                  dict(event='scheduler-target', path='0x100', value=1, after=0x4000000, accLimit=5000),
                  dict(event='scheduler-admission', path='0x100', request=1, bucketOffset=0x1c, result=1), search]
        for tick in range(1, 301):
            if tick == 80:
                events.extend([marker(tick, 'before_target_move'), marker(tick, 'target_move_accepted'), later])
            y = -144 + min(20, max(0, tick - 80)) * 12.8
            events.extend([dict(event='target-marker', value=f'PATHTARGET tick={tick} x=-1936.000 y={y:.3f} order=0'), marker(tick, 'sample')])
        events.append(marker(300, 'complete'))
        rows[1:1] = events
        self.assertEqual(trace.analyze(rows, 'follow_walk')['violations'], [])
        later['path'] = '0x999'
        self.assertTrue(trace.analyze(rows, 'follow_walk')['violations'])
        later['path'] = '0x100'
        reuse = dict(event='group-target', path='0x100', target='0x0')
        rows.insert(rows.index(later), reuse)
        self.assertTrue(trace.analyze(rows, 'follow_walk')['violations'])
        rows.remove(reuse)
        target81 = next(r for r in events if r.get('event') == 'target-marker' and 'tick=81 ' in r['value'])
        target81['value'] = 'PATHTARGET tick=81 x=-1936.000 y=112.000 order=0'
        self.assertTrue(trace.analyze(rows, 'follow_walk')['violations'])

    def test_stopped_path_reset_retains_original_goal(self):
        row = dict(event='path-destination', caller='0x171415', destination=[-128000, -128000],
                   original=[163.5, 91.5], replaceOriginal=0, afterFlags=0x200000,
                   counts=[0, 0], indices=[-1, -1])
        rows = capture()
        rows.insert(1, row)
        self.assertEqual(trace.analyze(rows)['stopped_path_resets'], 1)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['replaceOriginal'] = 1
        self.assertTrue(trace.analyze(rows)['violations'])
        row['replaceOriginal'] = 0
        row['counts'] = [1, 0]
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_replan_distinguishes_changed_from_ready(self):
        row = dict(event='replan-check', oldDestination=[1.5, 0], destination=[2.0, 0],
                   shift=1, timestamps=[90, 91], counter=100, changed=1, ready=0)
        rows = capture()
        rows.insert(1, row)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['ready'] = 1
        self.assertTrue(trace.analyze(rows)['violations'])
        row['destination'] = [1.9, 0]
        row['changed'] = 0
        self.assertEqual(trace.analyze(rows)['violations'], [])

    def test_arrival_result_requires_range_and_heading_tolerance(self):
        row = dict(event='arrival-transition', threshold=11.3125, footprint=0.96875,
                   angle=0.2, inRange=1, result=1)
        rows = capture()
        rows.insert(1, row)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['angle'] = 0.21
        self.assertTrue(trace.analyze(rows)['violations'])
        row['result'] = 0
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['angle'] = 0
        row['inRange'] = 0
        self.assertEqual(trace.analyze(rows)['violations'], [])
        row['result'] = 1
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_follow_requires_target_path_and_bucket_evidence(self):
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=-1936.000 y=-976.000 order=0')
        target = dict(event='group-target', path='0x100', target='0x200')
        switch = dict(event='scheduler-target', path='0x100', value=1, after=0x4000000, accLimit=5000)
        admission = dict(event='scheduler-admission', path='0x100', request=1, bucketOffset=0x1c, result=1)
        search = dict(event='search', kind='acc', path='0x100', request=1, budget=5000,
                      nodes=1, pops=1, result=0, levels={'0': 1})
        rows = capture(search)
        rows[1:1] = [marker(0, 'start_follow'), marker(10, 'order_accepted'), target, switch, admission,
                     *[marker(tick, 'sample') for tick in range(1, 301)], marker(300, 'complete')]
        self.assertEqual(trace.analyze(rows, 'follow')['violations'], [])
        admission['bucketOffset'] = 0
        self.assertTrue(trace.analyze(rows, 'follow')['violations'])
        admission['bucketOffset'] = 0x1c
        switch['path'] = '0x300'
        self.assertTrue(trace.analyze(rows, 'follow')['violations'])

    def test_owner_change_requires_synchronous_row_transition(self):
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=-1936.000 y=-976.000 order=0')
        change = dict(event='scheduler-class', value=1, before=0x200000, after=0x210000)
        rows = capture()
        rows[1:1] = [marker(0, 'start_owner_change'), marker(10, 'order_accepted'),
                     marker(15, 'before_owner_change'), change, marker(15, 'after_owner_change'),
                     *[marker(tick, 'sample') for tick in range(1, 301)], marker(300, 'complete')]
        self.assertEqual(trace.analyze(rows, 'owner_change')['violations'], [])
        change['after'] = 0
        self.assertTrue(trace.analyze(rows, 'owner_change')['violations'])
        change['after'] = 0x210000
        rows.remove(change)
        rows.insert(-1, change)
        self.assertTrue(trace.analyze(rows, 'owner_change')['violations'])

    def test_gate_edit_requires_route_before_edit_and_no_early_traversal(self):
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=-1936.000 y=-976.000 order=0')
        rows = capture(calls={'gate-traversal': 1})
        route = dict(event='route', kind='acc', count=3, truncated=False,
                     points=[[81.75, 45.75], [-128000.0078125, 1], [81.75, 32.75]])
        traversal = dict(event='gate-traversal', result=1)
        rows[1:1] = [marker(0, 'start_gate_retarget'), marker(0, 'gate_active'),
                     marker(10, 'order_accepted'), route, marker(15, 'before_gate_retarget'),
                     marker(15, 'after_gate_retarget'), traversal,
                     *[marker(tick, 'sample') for tick in range(1, 301)], marker(300, 'complete')]
        result = trace.analyze(rows, 'gate_retarget')
        self.assertEqual(result['violations'], [])
        self.assertEqual(result['gate_change']['traversals_after_edit'], 1)
        rows.remove(traversal)
        rows.insert(2, traversal)
        self.assertTrue(trace.analyze(rows, 'gate_retarget')['violations'])
        rows.remove(traversal)
        rows.insert(-1, traversal)
        rows.remove(route)
        rows.insert(-1, route)
        self.assertTrue(trace.analyze(rows, 'gate_retarget')['violations'])

    def test_gate_entry_requires_matching_completion_and_success(self):
        rows = capture(calls={'gate-traversal': 1})
        self.assertTrue(trace.analyze(rows)['violations'])
        rows.insert(1, dict(event='gate-traversal', result=0))
        self.assertEqual(trace.analyze(rows)['violations'], [])
        self.assertEqual(trace.analyze(rows)['gate_traversals']['successful'], 0)
        rows[1]['result'] = 1
        self.assertEqual(trace.analyze(rows)['gate_traversals']['successful'], 1)

    def test_cell_snapshot_requires_consistent_levels_and_words(self):
        rows = capture()
        cells = [dict(level=l, x=163 >> (l + 1), y=78 >> (l + 1), word=0,
                      classes=None if l == -1 else [0, 0, 0, 0]) for l in range(-1, 4)]
        rows.insert(1, dict(event='cell-snapshot', cells=cells))
        self.assertEqual(trace.analyze(rows)['violations'], [])
        cells[1]['classes'][0] = 1
        self.assertTrue(trace.analyze(rows)['violations'])
        cells[1]['classes'][0] = 0
        cells[1]['x'] += 1
        self.assertTrue(trace.analyze(rows)['violations'])
        cells.pop()
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_route_dump_rejects_inconsistent_or_nonfinite_points(self):
        rows = capture()
        route = dict(event='route', count=2, points=[[1, 2], [3, 4]], truncated=False)
        rows.insert(1, route)
        self.assertEqual(trace.analyze(rows)['violations'], [])
        route['count'] = 3
        self.assertTrue(trace.analyze(rows)['violations'])
        route['count'] = 2
        route['points'][1][0] = float('nan')
        self.assertTrue(trace.analyze(rows)['violations'])

    def test_empty_and_truncated_captures_are_not_evidence(self):
        self.assertTrue(trace.analyze([])['violations'])
        rows = capture()
        self.assertEqual(trace.analyze(rows)['searches']['acc']['sampled'], 0)
        self.assertTrue(trace.analyze(rows[:-1])['violations'])

    def test_budget_exhaustion_is_not_reported_as_disconnection(self):
        row = dict(event='search', kind='fine', nodes=734, pops=701, budget=700, result=-1, footprintClass=2)
        result = trace.analyze(capture(row))
        self.assertEqual(result['violations'], [])
        self.assertEqual(result['searches']['fine']['sampled_budget_stops'], 1)
        row['pops'] = 20
        self.assertEqual(trace.analyze(capture(row))['searches']['fine']['sampled_budget_stops'], 0)

    def test_sampling_cap_does_not_erase_unsampled_calls(self):
        row = dict(event='search', kind='acc', nodes=5, pops=2, budget=400, result=1, levels={'0': 2, '2': 3})
        result = trace.analyze(capture(row, calls={'acc-search': 100}))
        self.assertEqual(result['violations'], [])
        self.assertEqual(result['searches']['acc']['sampled'], 1)
        self.assertEqual(result['searches']['acc']['calls'], 100)
        self.assertEqual(result['searches']['acc']['node_levels_summed_over_samples'], {'0': 2, '2': 3})

    def test_corrupt_layout_and_observer_errors_reject_capture(self):
        row = dict(event='search', kind='acc', nodes=5, pops=2, budget=400, result=1, levels={'4': 5})
        self.assertTrue(trace.analyze(capture(row))['violations'])
        row['levels'] = {'0': 4}
        self.assertTrue(trace.analyze(capture(row))['violations'])
        row['levels'] = {'0': 5}
        rows = capture(row)
        rows.insert(1, {'type': 'error', 'description': 'bad memory read'})
        self.assertTrue(trace.analyze(rows)['violations'])


    def test_scenario_requires_all_ticks_and_accepted_order(self):
        rows = capture()
        def marker(tick, label):
            return dict(event='marker', value=f'PATHTRACE tick={tick} label={label} x=-1936.000 y=-976.000 order=0')
        rows[1:1] = [marker(0, 'start_open'), marker(10, 'order_accepted'),
                     *[marker(tick, 'sample') for tick in range(1, 301)], marker(300, 'complete')]
        self.assertEqual(trace.analyze(rows, 'open')['violations'], [])
        del rows[50]
        self.assertTrue(trace.analyze(rows, 'open')['violations'])
        self.assertTrue(trace.analyze(rows, 'wall')['violations'])


class ProbeMapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parents[1]
        spec = importlib.util.spec_from_file_location('path_map', root / 'tools/frida/make_wc3_pathfinding_map.py')
        cls.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.builder)
        cls.probe = (root / 'tools/frida/wc3_pathfinding_probe.j').read_text()
        cls.source = "globals\ninteger original = 1\nendglobals\nfunction main takes nothing returns nothing\n" + \
            "call CreateAllUnits(  )\ncall InitCustomTriggers(  )\ncall RunInitializationTriggers(  )\n" + \
            "set gg_rct_SorcAFight = Rect( -1856.0, -352.0, -1728.0, -224.0 )\nendfunction\n"

    def test_control_and_edit_have_same_terrain_script_except_scenario(self):
        output = self.builder.instrument(self.source, self.probe, 'insert')
        self.assertEqual(output.count('globals\n'), 2)  # globals and endglobals
        self.assertIn('integer original = 1', output)
        self.assertIn('PATH_PROBE_SCENARIO = 2', output)
        self.assertNotIn('call CreateAllUnits(', output)
        self.assertNotIn('@SCENARIO@', output)
        self.assertNotIn('@NAME@', output)
        self.assertLess(output.index('function PathProbeInit'), output.index('function main'))
        self.assertIn('call PathProbeInit()', output)

    def test_removal_time_must_be_inside_measurement(self):
        for tick in (0, 10, 300):
            with self.assertRaises(ValueError):
                self.builder.instrument(self.source, self.probe, 'remove', tick)
        output = self.builder.instrument(self.source, self.probe, 'remove', 20)
        self.assertIn('udg_PathProbeTick == 20 and (PATH_PROBE_SCENARIO == 3', output)
        self.assertNotIn('@REMOVE_TICK@', output)

    def test_captain_player_is_computer_before_ai_agent_allocation(self):
        config = 'function config takes nothing returns nothing\ncall SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\nendfunction\n'
        output = self.builder.instrument(config+self.source, self.probe, 'captain_home')
        self.assertIn('PATH_PROBE_SCENARIO = 71', output)
        self.assertIn('call SetPlayerController( Player(0), MAP_CONTROL_COMPUTER )', output)
        self.assertIn('call StartCampaignAI(Player(0),"Scripts\\\\wc3_captain_probe.ai")', output)
        self.assertNotIn('call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )', output)

    def test_missing_or_ambiguous_captain_config_is_rejected(self):
        with self.assertRaises(ValueError):
            self.builder.instrument(self.source, self.probe, 'captain_home')
        config = 'call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\n'
        with self.assertRaises(ValueError):
            self.builder.instrument(config+config+self.source, self.probe, 'captain_home')

    def test_captain_pool_lifecycle_is_public_and_explicit(self):
        config='call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\n'
        for mode in ('reuse','transfer','same_owner','partial'):
            out=self.builder.instrument(config+self.source,self.probe,'captain_home',captain_peer=True,captain_pool=mode)
            self.assertIn('PATHCAPTAIN pool after recruit',out)
            self.assertIn('udg_PathProbeCrowd[1]=CreateUnit',out)
            if mode=='reuse':
                self.assertIn('udg_PathProbeTick == 5 then',out)
                self.assertIn('udg_PathProbeTick == 20 then',out)
                self.assertIn('call RemoveUnit(udg_PathProbeUnit)',out)
            elif mode in ('transfer','partial'):
                self.assertIn('call SetUnitOwner(udg_PathProbeUnit,Player(1),false)',out)
            else:
                self.assertNotIn('call SetUnitOwner(udg_PathProbeUnit,Player(1),false)',out)
        for options in ({},{'captain_peer':True,'captain_peer_type':'hkni'},
                        {'captain_peer':True,'captain_blocked_home':True}):
            with self.assertRaises(ValueError):
                self.builder.instrument(config+self.source,self.probe,'captain_home',captain_pool='reuse',**options)

    def test_captain_third_is_explicit_and_preserves_birth_order(self):
        config='call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\n'
        out=self.builder.instrument(config+self.source,self.probe,'captain_home',captain_peer=True,captain_third=True)
        self.assertIn("udg_PathProbeCrowd[2]=CreateUnit(Player(0),'hfoo',-1776.0,-976.0,90.0)",out)
        self.assertLess(out.index('udg_PathProbeCrowd[1]=CreateUnit'),out.index('udg_PathProbeCrowd[2]=CreateUnit'))
        for opts in ({},{'captain_peer':True,'captain_pool':'transfer'},
                     {'captain_peer':True,'captain_peer_type':'hkni'},
                     {'captain_peer':True,'captain_blocked_home':True}):
            with self.assertRaises(ValueError):
                self.builder.instrument(config+self.source,self.probe,'captain_home',captain_third=True,**opts)

    def test_captain_thirteen_births_have_explicit_grid_order(self):
        config='call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\n'
        out=self.builder.instrument(config+self.source,self.probe,'captain_home',captain_thirteen=True)
        self.assertIn("udg_PathProbeCrowd[12]=CreateUnit(Player(0),'hfoo',-1936.0,-1216.0,90.0)",out)
        self.assertLess(out.index('udg_PathProbeCrowd[1]=CreateUnit'),out.index('udg_PathProbeCrowd[12]=CreateUnit'))
        for opts in ({'captain_peer':True},{'captain_third':True},
                     {'captain_pool':'transfer'},{'captain_blocked_home':True}):
            with self.assertRaises(ValueError):
                self.builder.instrument(config+self.source,self.probe,'captain_home',captain_thirteen=True,**opts)

    def test_captain_lifetime_uses_public_mutations_and_declares_before_use(self):
        config='call SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )\n'
        for operation,mode in enumerate(('remove','retarget','stop')):
            out=self.builder.instrument(config+self.source,self.probe,'captain_home',captain_thirteen=True,captain_lifetime=mode)
            self.assertLess(out.index('function PathCaptainLifetimeRecord'),out.index('function PathCaptainLifetimeTick'))
            self.assertLess(out.index('function PathCaptainLifetimeTick'),out.index('function PathProbeTick'))
            self.assertIn(f'call PathCaptainLifetimeTick({operation})',out)
            self.assertIn("local integer crowdType = 'hCLG'",out)
            self.assertIn('udg_PathProbeTick == 400 and PATH_PROBE_SCENARIO!=64',out)
            self.assertIn('call RemoveUnit(udg_PathProbeCrowd[i])',out)
            self.assertIn('call IssuePointOrder(udg_PathProbeCrowd[i],"move"',out)
            self.assertIn('call SaveReal(udg_CaptainLifetimeTable,row,5,GetUnitY',out)
        with self.assertRaises(ValueError):
            self.builder.instrument(config+self.source,self.probe,'captain_home',captain_lifetime='stop')

    def test_gate_geometry_is_explicit_and_finite(self):
        for y in (float('nan'), float('inf'), -4000):
            with self.assertRaises(ValueError):
                self.builder.instrument(self.source, self.probe, 'gate', gate_y=y)
        output = self.builder.instrument(self.source, self.probe, 'gate', gate_y=-640, gate_exit_y=-432)
        self.assertIn("'nwgt', -1936.0, -640.0", output)
        self.assertIn('WaygateSetDestination(udg_PathProbeGate, -1936.0, -432.0)', output)

    def test_gate_control_only_changes_scenario_selection(self):
        active = self.builder.instrument(self.source, self.probe, 'gate')
        inactive = self.builder.instrument(self.source, self.probe, 'gate_off')
        self.assertEqual(active.replace('PATH_PROBE_SCENARIO = 5', 'PATH_PROBE_SCENARIO = 6')
                         .replace('start_gate"', 'start_gate_off"')
                         .replace('pathtrace-gate.txt', 'pathtrace-gate_off.txt'), inactive)
        self.assertIn("'nwgt'", active)
        self.assertIn('WaygateSetDestination', active)

    def test_speed_scene_preserves_object_rows_and_authors_integer_limits(self):
        original = b'hpea' + bytes(4) + struct.pack('<I', 1) + b'unam' + struct.pack('<I', 3) + b'original\0' + bytes(4)
        custom = b'hfoo' + b'h002' + struct.pack('<I', 1) + b'umvs' + struct.pack('<Ii', 0, 300) + b'h002'
        source = struct.pack('<II', 1, 1) + original + struct.pack('<I', 1) + custom
        output = self.builder.speed_unit_limits(source)
        self.assertEqual(output[:8 + len(original)], source[:8 + len(original)])
        self.assertEqual(output[12 + len(original):len(source)], source[12 + len(original):])
        self.assertEqual(struct.unpack_from('<I', output, 8 + len(original))[0], 2)
        expected = b'hfoo' + b'h001' + struct.pack('<I', 3)
        for field, value in ((b'umvs', 237), (b'umis', 173), (b'umas', 389)):
            expected += field + struct.pack('<Ii', 0, value) + b'h001'
        self.assertEqual(output[len(source):], expected)
        with self.assertRaises(ValueError): self.builder.speed_unit_limits(output)
        with self.assertRaises(ValueError): self.builder.speed_unit_limits(source + b'junk')
        script = self.builder.instrument(self.source, self.probe, 'speed_inputs')
        self.assertIn('PATH_PROBE_SCENARIO = 32', script)
        self.assertIn("CreateUnit(Player(0), 'h001'", script)
        self.assertIn('GetUnitDefaultMoveSpeed', script)

    def test_profile_scene_creates_all_authored_movement_types(self):
        output = self.builder.instrument(self.source, self.probe, 'profiles')
        self.assertIn('PATH_PROBE_SCENARIO = 31', output)
        for rawcode in ('hkni', 'hgry', 'hsor', 'hbot', 'uplg', 'halt'):
            self.assertIn("CreateUnit(Player(0), '" + rawcode + "'", output)
        self.assertIn('label=profiles_created', output)

    def test_unrecognized_source_cannot_be_silently_instrumented(self):
        for source in (self.source.replace('call InitCustomTriggers(  )', ''),
                       self.source + 'call CreateAllUnits(  )',
                       self.source.replace('-1856.0', '-1855.0')):
            with self.assertRaises(ValueError):
                self.builder.instrument(source, self.probe, 'open')


if __name__ == '__main__':
    unittest.main()
