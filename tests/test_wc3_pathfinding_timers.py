"""Reject incomplete timer evidence and keep getter-driven gameplay inputs exact."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_timer_trace import normalize,verify

class TimerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-inputs-1.27.json').read_text())
        cls.raw=[gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-timer-inputs-1.27-{tag}.jsonl.gz').read_bytes())for tag in ('b','c')]
        cls.rows=[json.loads(line)for line in cls.raw[0].splitlines()]

    def changed(self,rows):
        raw=b'\n'.join(json.dumps(row).encode()for row in rows)+b'\n'
        cap=copy.deepcopy(self.fixture['captures'][0])
        cap.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
        return verify(raw,self.fixture,cap)

    def test_two_complete_repeats_preserve_all_getters_and_movement(self):
        for raw,cap in zip(self.raw,self.fixture['captures']):
            self.assertEqual(verify(raw,self.fixture,cap),dict(public_getters=1600,scalar_getters=2037,motion_commits=70,timeouts=533))
        self.assertEqual(len(self.fixture['events']),4831)
        self.assertIn('capturing them is not engine parity',self.fixture['scope'])

    def test_hash_metadata_footer_and_terminal_marker_are_required(self):
        cap=self.fixture['captures'][0]
        with self.assertRaisesRegex(ValueError,'hash/length'):verify(self.raw[0]+b'\n',self.fixture,cap)
        bad=copy.deepcopy(self.rows);bad[0]['source_sha256']['wc3_pathfinding.js']='0'*64
        with self.assertRaisesRegex(ValueError,'provenance'):self.changed(bad)
        bad=[r for r in self.rows if r.get('event')!='trace-end']
        with self.assertRaisesRegex(ValueError,'incomplete'):self.changed(bad)
        bad=self.rows+[self.rows[-1]]
        with self.assertRaisesRegex(ValueError,'incomplete'):self.changed(bad)
        bad=[r for r in self.rows if r.get('value')!='PATHTIMER complete']
        with self.assertRaisesRegex(ValueError,'completion'):self.changed(bad)
        with self.assertRaisesRegex(ValueError,'incomplete'):self.changed(self.rows+[dict(type='error')])

    def test_scalar_outputs_inputs_clocks_methods_and_movement_are_strict(self):
        for event,key,index in [('timer-getter','word',None),('timer-start','timeout',None),
                ('timer-scalar-getter','stored',0),('timer-scalar-getter','clockMethods',6),
                ('timer-getter','clock',1),('velocity-commit','after',2),('velocity-commit','after',5)]:
            bad=copy.deepcopy(self.rows);r=next(r for r in bad if r.get('event')==event)
            if index is None:r[key]^=1
            else:r[key][index]^=1
            with self.subTest(event=event,key=key,index=index),self.assertRaisesRegex(ValueError,'words/lifecycle'):self.changed(bad)
        for value in (0.0,False,-1,0x100000000):
            bad=copy.deepcopy(self.rows);next(r for r in bad if r.get('event')=='timer-getter')['word']=value
            with self.subTest(value=value),self.assertRaisesRegex(ValueError,'uint32'):self.changed(bad)

    def test_missing_extra_reordered_events_and_changed_identity_fail(self):
        i=next(i for i,r in enumerate(self.rows)if r.get('event')=='timer-getter')
        for bad in (self.rows[:i]+self.rows[i+1:],self.rows[:i]+[self.rows[i]]+self.rows[i:]):
            with self.assertRaisesRegex(ValueError,'words/lifecycle'):self.changed(bad)
        bad=copy.deepcopy(self.rows);bad[i],bad[i+1]=bad[i+1],bad[i]
        with self.assertRaisesRegex(ValueError,'words/lifecycle'):self.changed(bad)
        bad=copy.deepcopy(self.rows);bad[i]['handle']='0x12345678'
        with self.assertRaisesRegex(ValueError,'words/lifecycle'):self.changed(bad)
        # Addresses can vary across processes, but encounter identity/order cannot.
        bad=copy.deepcopy(self.rows)
        for r in bad:
            for key in ('timer','handle'):
                if key in r and type(r[key])is str:r[key]='relocated:'+r[key]
        self.assertEqual(normalize(bad),self.fixture['events'])

    def test_engine_motion_and_producer_equal_frozen_native_inputs(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_timer_motion_116.h').read_text()
        table=source.split('timer_motion_116[][7]={',1)[1].split('};',1)[0]
        motion=[[0,r['clock'][0],*r['position'],*r['velocity'],r['heading']]for r in self.fixture['events']if r['event']=='velocity-commit']
        self.assertEqual([int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',table)],[w for row in motion for w in row])
        body=source.split('timer_script_116[]=',1)[1].split('/* Original public116',1)[0]
        script=''.join(json.loads(line.strip().rstrip(';'))for line in body.splitlines()if line.strip().startswith('"'))
        original=(ROOT/'tools/frida/wc3_timer_inputs_probe.j').read_text()
        expected=original.replace('@SCENARIO@','116').replace("'hfoo'","'hT16'")
        expected=expected.replace('globals\n','globals\n hashtable udg_PathTimeoutWords=null\n',1)
        expected=expected.replace(' call Preload("PATHTIMER read="',' call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i,TimerGetTimeout(udg_PathInputTimer[i]))\n call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i+13,TimerGetElapsed(udg_PathInputTimer[i]))\n call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i+26,TimerGetRemaining(udg_PathInputTimer[i]))\n call Preload("PATHTIMER read="',1)
        expected=expected.replace(' local timer moveTimer=CreateTimer()\n',' local timer moveTimer=CreateTimer()\n set udg_PathTimeoutWords=InitHashtable()\n',1)
        expected+='function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'
        self.assertEqual(script,expected)
        # The compiled producer's adjacent literals are distinct raw words.
        self.assertEqual(self.fixture['timeout_words'][7:10],[0x3dcccccc,0x3dccccce,0x3dcccccd])

    def test_capture_sources_are_frozen_and_timer_program_is_current(self):
        hashes=self.fixture['captures'][0]['metadata']['source_sha256']
        for name,digest in hashes.items():
            if name=='map':continue
            frozen=gzip.decompress((ROOT/f'tools/ghidra/fixtures/sources/{digest}.gz').read_bytes())
            self.assertEqual(hashlib.sha256(frozen).hexdigest(),digest)
        self.assertEqual(hashlib.sha256((ROOT/'tools/frida/wc3_timer_inputs_probe.j').read_bytes()).hexdigest(),hashes['wc3_timer_inputs_probe.j'])

    def test_saved_ghidra_types_abi_and_function_map_match(self):
        static=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-getters-116-static.json').read_text())
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        self.assertFalse(static['unsaved']);self.assertTrue(static['passed'])
        self.assertEqual(len(static['functions']),11)
        for f in static['functions']:
            self.assertTrue(f['decompiled']);self.assertIn('"'+f['address']+'", "'+f['name']+'"',mapping)
            method=next(m for m in schema['methods']if m['address']==f['address'])
            self.assertEqual(method['name'],f['name'])
            self.assertTrue(f['returns'])
            cleanup=0 if method['convention']=='__cdecl'else 4
            self.assertTrue(all(r['cleanup']==cleanup for r in f['returns']))
        for layout in static['layouts']:
            declared=next(l for l in schema['layouts']if l['name']==layout['name'])
            self.assertLessEqual(layout['length'],declared['length'])
            # Later evidence may name undefined bytes, while retaining every prior field.
            declared_fields={(f['offset'],f['name'])for f in declared['fields']}
            self.assertTrue({(f['offset'],f['name'])for f in layout['fields']}<=declared_fields)

class TimerEpochTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-epoch-1.27.json').read_text())
        cls.raw=[gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-timer-epoch-1.27-{tag}.jsonl.gz').read_bytes())for tag in ('b','c')]
        cls.rows=[json.loads(line)for line in cls.raw[0].splitlines()]

    def test_complete_repeats_include_elapsed_remaining_control_and_epoch(self):
        for raw,cap in zip(self.raw,self.fixture['captures']):
            self.assertEqual(verify(raw,self.fixture,cap),dict(public_getters=13692,scalar_getters=17328,motion_commits=320,timeouts=4563))
        self.assertEqual(len(self.fixture['events']),40551)
        self.assertEqual({r['clock'][1]for r in self.fixture['events']if r['event']=='velocity-commit'},{0,1})
        self.assertIn('NUM-02.10',self.fixture['scope'])

    def test_all_getter_tables_and_motion_words_equal_native_capture(self):
        for filename,name,ticks in [('retail_timer_motion_116.h','timer_getters_117',41),('retail_timer_motion_117.h','timer_epoch_getters_117',351)]:
            f=self.fixture if ticks==351 else json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-inputs-1.27.json').read_text())
            table=[[[None]*3 for _ in range(13)]for _ in range(ticks)];read=None
            for r in f['events']:
                if r['event']=='timer-marker' and (match:=re.fullmatch(r'PATHTIMER read=(\d+) tick=(\d+)',r['value'])):read=tuple(map(int,match.groups()))
                if r['event']=='timer-getter' and read:
                    actor,tick=read;k=['timeout','elapsed','remaining'].index(r['name'])
                    if table[tick][actor][k]is None:table[tick][actor][k]=r['word']
            source=(ROOT/'games/warcraft-3/game/tests'/filename).read_text()
            body=source.split(name+'[',1)[1].split('={',1)[1].split('};',1)[0]
            actual=[int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',body)]
            expected=[w for tick in table for actor in tick for w in actor]
            self.assertNotIn(None,expected);self.assertEqual(actual,expected)
        motion=source.split('timer_motion_117[][7]={',1)[1].split('};',1)[0]
        actual=[int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',motion)]
        expected=[w for r in self.fixture['events']if r['event']=='velocity-commit'for w in [0,r['clock'][0],*r['position'],*r['velocity'],r['heading']]]
        self.assertEqual(actual,expected)
        body=source.split('timer_script_117[]=',1)[1].split('static uint32_t const timer_epoch_getters_',1)[0]
        script=''.join(json.loads(line.strip().rstrip(';'))for line in body.splitlines()if line.strip().startswith('"'))
        original=(ROOT/'tools/frida/wc3_timer_boundaries_probe.j').read_text().replace('@SCENARIO@','117').replace("'hfoo'","'hT16'")
        original=original.replace('globals\n','globals\n hashtable udg_PathTimeoutWords=null\n',1)
        original=original.replace(' call Preload("PATHTIMER read="',' call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i,TimerGetTimeout(udg_PathInputTimer[i]))\n call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i+13,TimerGetElapsed(udg_PathInputTimer[i]))\n call SaveReal(udg_PathTimeoutWords,udg_PathProbeTick,i+26,TimerGetRemaining(udg_PathInputTimer[i]))\n call Preload("PATHTIMER read="',1)
        original=original.replace(' local timer moveTimer=CreateTimer()\n',' local timer moveTimer=CreateTimer()\n set udg_PathTimeoutWords=InitHashtable()\n',1)
        original+='function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'
        self.assertEqual(script,original)

    def test_clock_segment_remainder_deadline_and_serial_mutations_fail(self):
        for key,index in [('timerClock',0),('timerClock',1),('segments',None),('remainingSegments',None),('residual',None),('request',0),('request',3)]:
            bad=copy.deepcopy(self.rows);r=next(r for r in bad if r.get('event')=='timer-scalar-getter')
            target=r if key=='timerClock'else r['control']
            if index is None:target[key]^=1
            else:target[key][index]^=1
            raw=b'\n'.join(json.dumps(row).encode()for row in bad)+b'\n';cap=copy.deepcopy(self.fixture['captures'][0])
            cap.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
            with self.subTest(key=key,index=index),self.assertRaisesRegex(ValueError,'words/lifecycle'):verify(raw,self.fixture,cap)

    def test_frozen_provenance_and_saved_ghidra_layouts(self):
        for name,digest in self.fixture['captures'][0]['metadata']['source_sha256'].items():
            source=gzip.decompress((ROOT/f'tools/ghidra/fixtures/sources/{digest}.gz').read_bytes())
            self.assertEqual(hashlib.sha256(source).hexdigest(),digest)
        static=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-epoch-117-static.json').read_text())
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        self.assertFalse(static['unsaved']);self.assertTrue(static['passed']);self.assertEqual(len(static['functions']),11)
        for f in static['functions']:
            self.assertTrue(f['decompiled']);self.assertIn('"'+f['address']+'", "'+f['name']+'"',mapping)
            m=next(m for m in schema['methods']if m['address']==f['address']);self.assertEqual(m['name'],f['name'])
            cleanup=sum(4 for p in m['parameters']if type(p['storage'])is int)
            if f['address']!='6f002170':self.assertTrue(f['returns'])
            self.assertTrue(all(r['cleanup']==cleanup for r in f['returns']))
        for layout in static['layouts']:
            declared=next(l for l in schema['layouts']if l['name']==layout['name'])
            self.assertEqual(layout['length'],declared['length'])
            self.assertTrue({(f['offset'],f['name'])for f in layout['fields']} <= {(f['offset'],f['name'])for f in declared['fields']})


class TimerMutationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-mutation-1.27.json').read_text())
        cls.raw=[gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-timer-mutation-1.27-{tag}.jsonl.gz').read_bytes())for tag in ('b','c')]
        cls.rows=[json.loads(line)for line in cls.raw[0].splitlines()]

    def test_complete_mutation_repeats(self):
        for raw,cap in zip(self.raw,self.fixture['captures']):
            counts=verify(raw,self.fixture,cap)
            self.assertEqual((counts['records'],counts['public_getters'],counts['motion_commits']),(348,1044,175))
            self.assertEqual(counts['timer-dispatch-begin'],163)
        self.assertEqual(len(self.fixture['events']),2986)

    def test_dispatch_serial_cancel_resume_fields_and_event_order_are_strict(self):
        for event,key,index in [('timer-dispatch-begin','words',4),('timer-dispatch-begin','words',3),
                ('timer-dispatch-end','after',0),('timer-dispatch-end','after',3),
                ('timer-control-end','stored',2),('timer-cancel','flags',None)]:
            bad=copy.deepcopy(self.rows);row=next(r for r in bad if r.get('event')==event)
            if index is None:row[key]^=1
            else:row[key][index]^=1
            raw=b'\n'.join(json.dumps(r).encode()for r in bad)+b'\n';cap=copy.deepcopy(self.fixture['captures'][0])
            cap.update(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
            with self.subTest(event=event,key=key,index=index),self.assertRaisesRegex(ValueError,'words/lifecycle'):verify(raw,self.fixture,cap)
        actual=normalize(self.rows,True,True)
        first=next(i for i,r in enumerate(actual)if r['event']=='timer-marker'and r['value'].startswith('PATHTIMER record='))
        self.assertTrue(actual[first]['value'].startswith('PATHTIMER record=205 '))

    def test_callback_and_motion_tables_equal_native_words(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_timer_motion_118.h').read_text()
        records=[];current=None
        for r in self.rows:
            if r.get('event')=='timer-marker'and (m:=re.fullmatch(r'PATHTIMER record=(\d+) row=(\d+) calls=(\d+)',r['value'])):
                ident,row,calls=map(int,m.groups());self.assertEqual(row,len(records));current=[ident,calls];records.append(current)
            if r.get('event')=='timer-getter'and current is not None:current.append(r['word'])
        body=source.split('timer_records_118[][5]={',1)[1].split('};',1)[0]
        self.assertEqual([int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',body)],[w for row in records for w in row])
        body=source.split('timer_motion_118[][7]={',1)[1].split('};',1)[0]
        self.assertEqual([int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',body)],
            [w for r in self.fixture['events']if r['event']=='velocity-commit'for w in [0,r['clock'][0],*r['position'],*r['velocity'],r['heading']]])
        body=source.split('timer_script_118[]=')[1]
        script=''.join(json.loads(line.strip().rstrip(';'))for line in body.splitlines()if line.strip().startswith('"'))
        expected=(ROOT/'tools/frida/wc3_timer_mutation_probe.j').read_text().replace('@SCENARIO@','118').replace("'hfoo'","'hT16'")
        expected=expected.replace('globals\n','globals\n hashtable udg_PathTimerWords=null\n',1)
        expected=expected.replace(' call Preload("PATHTIMER record="',
            ' call SaveInteger(udg_PathTimerWords,udg_PathTimerRow,0,id)\n call SaveInteger(udg_PathTimerWords,udg_PathTimerRow,1,udg_PathCalls[PathTimerIdentity(t)])\n call SaveReal(udg_PathTimerWords,udg_PathTimerRow,2,TimerGetTimeout(t))\n call SaveReal(udg_PathTimerWords,udg_PathTimerRow,3,TimerGetElapsed(t))\n call SaveReal(udg_PathTimerWords,udg_PathTimerRow,4,TimerGetRemaining(t))\n call Preload("PATHTIMER record="',1)
        expected=expected.replace(' local trigger t=null\n',' local trigger t=null\n set udg_PathTimerWords=InitHashtable()\n',1)
        expected+='function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'
        self.assertEqual(script,expected)

    def test_saved_ghidra_and_frozen_producers(self):
        f=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-mutation-118-static.json').read_text())
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        mapped=set(re.findall(r'^\s*\{\s*"([0-9a-f]+)"\s*,\s*"([^"]+)"',mapping,re.M))
        self.assertTrue(f['passed']);self.assertFalse(f['unsaved']);self.assertEqual(len(f['functions']),9)
        for fun in f['functions']:
            self.assertTrue(fun['decompiled']);self.assertIn((fun['address'],fun['name']),mapped)
        for layout in f['layouts']:
            declared=next(x for x in schema['layouts']if x['name']==layout['name'])
            self.assertEqual(layout['length'],declared['length'])
            self.assertEqual([(x['offset'],x['name'])for x in layout['fields']],[(x['offset'],x['name'])for x in declared['fields']])
        for fixture in (self.fixture,json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-release-1.27.json').read_text())):
            for name,digest in fixture['captures'][0]['metadata']['source_sha256'].items():
                raw=gzip.decompress((ROOT/f'tools/ghidra/fixtures/sources/{digest}.gz').read_bytes())
                self.assertEqual(hashlib.sha256(raw).hexdigest(),digest)

    def test_bounded_retirement_repeats_and_owner_tie_order(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-timer-release-1.27.json').read_text())
        for tag,cap in zip(('g','h'),fixture['captures']):
            raw=gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-timer-release-1.27-{tag}.jsonl.gz').read_bytes())
            result=verify(raw,fixture,cap);self.assertEqual(result['retirements'],4);self.assertEqual(result['owner_rearms'],6)
        own=[r for r in fixture['events']if r['event']=='public-timer-rearm']
        self.assertEqual(own[4]['before'],[0x3e19999a,0x3cf5c290,1,21])
        self.assertIn('before the full public scene ends',fixture['scope'])
        bad=copy.deepcopy(fixture['events']);bad[0]['counter']^=1
        with self.assertRaisesRegex(ValueError,'lifecycle'):
            wrong=copy.deepcopy(fixture);wrong['events']=bad;verify(raw,wrong,cap)

if __name__=='__main__':unittest.main()
