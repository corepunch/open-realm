"""Pin ordinary modal admission, exported words, engine input and saved Ghidra evidence."""
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
from verify_wc3_metadata_trace import normalize,verify

class MetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scenes=[]
        for label,tags,prefix in [('defend',('b','c'),'metadata'),('rally',('d','e'),'metadata_rally'),('busy',('f','g'),'metadata_busy')]:
            f=json.loads((ROOT/f'tools/ghidra/fixtures/retail-metadata-{label}-1.27.json').read_text())
            raw=[gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-metadata-{label}-1.27-{t}.jsonl.gz').read_bytes())for t in tags]
            cls.scenes.append((label,prefix,f,raw))
        cls.fixture=cls.scenes[0][2];cls.rows=[json.loads(s)for s in cls.scenes[0][3][0].splitlines()]

    def changed(self,rows):
        raw=b'\n'.join(json.dumps(r).encode()for r in rows)+b'\n';cap=copy.deepcopy(self.fixture['captures'][0]);cap.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw))
        return verify(raw,self.fixture,cap)

    def test_three_scenes_each_have_two_exact_complete_repeats(self):
        for label,prefix,f,raw in self.scenes:
            for data,cap in zip(raw,f['captures']):
                result=verify(data,f,cap);self.assertEqual(result['records'],f['records']);self.assertEqual(result['motion_commits'],f['motion_commits'])
        self.assertEqual(sum(f['records']for _,_,f,_ in self.scenes),210)
        self.assertEqual(sum(f['motion_commits']for _,_,f,_ in self.scenes),99)

    def test_hash_provenance_completion_and_error_controls(self):
        with self.assertRaisesRegex(ValueError,'hash/length'):verify(self.scenes[0][3][0]+b'\n',self.fixture,self.fixture['captures'][0])
        bad=copy.deepcopy(self.rows);bad[0]['source_sha256']['wc3_pathfinding.js']='0'*64
        with self.assertRaisesRegex(ValueError,'provenance'):self.changed(bad)
        for bad in ([r for r in self.rows if r.get('event')!='trace-end'],self.rows+[self.rows[-1]],self.rows+[dict(type='error')]):
            with self.assertRaisesRegex(ValueError,'incomplete'):self.changed(bad)
        with self.assertRaisesRegex(ValueError,'completion'):self.changed([r for r in self.rows if r.get('value')!='PATHMETA complete'])
        bad=copy.deepcopy(self.rows);next(r for r in bad if r.get('event')=='trace-end')['counts']['velocity-commit']+=1
        with self.assertRaisesRegex(ValueError,'footer motion'):self.changed(bad)

    def test_all_words_directions_flags_and_commits_are_strict(self):
        for event,key,index in [('metadata-row','word',None),('metadata-interception','result',None),
            ('metadata-toggle-validation','input',None),('metadata-toggle-validation','flags',None),
            ('metadata-defend-event','afterFlags',None),('velocity-commit','after',2),('velocity-commit','after',4)]:
            bad=copy.deepcopy(self.rows);r=next(r for r in bad if r.get('event')==event)
            if index is None:r[key]^=1
            else:r[key][index]^=1
            with self.subTest(event=event,key=key),self.assertRaisesRegex(ValueError,'words/admission'):self.changed(bad)
        for value in (False,1.0,-1,0x100000000):
            bad=copy.deepcopy(self.rows);next(r for r in bad if r.get('event')=='metadata-row')['word']=value
            with self.assertRaisesRegex(ValueError,'uint32'):self.changed(bad)

    def test_missing_added_and_reordered_exports_fail(self):
        i=next(i for i,r in enumerate(self.rows)if r.get('event')=='metadata-row')
        for bad in (self.rows[:i]+self.rows[i+1:],self.rows[:i]+[self.rows[i]]+self.rows[i:]):
            with self.assertRaisesRegex(ValueError,'words/admission'):self.changed(bad)
        bad=copy.deepcopy(self.rows);bad[i],bad[i+1]=bad[i+1],bad[i]
        with self.assertRaisesRegex(ValueError,'words/admission'):self.changed(bad)

    def test_addresses_and_void_eax_are_not_simulation_outputs(self):
        bad=copy.deepcopy(self.rows)
        for r in bad:
            for k in ('unit','ability','mover'):
                if k in r:r[k]='relocated:'+r[k]
            if r.get('event')=='metadata-clear-pending':r['result']=0x12345678
        self.assertEqual(normalize(bad),self.fixture['events'])

    def test_engine_tables_and_public_jass_equal_frozen_producers(self):
        for label,prefix,f,raw in self.scenes:
            source=(ROOT/f'games/warcraft-3/game/tests/retail_{prefix}_motion_119.h').read_text()
            tables = [
                (prefix+'_motion_119', [[0,r['clock'][0],*r['after'][2:6],r['after'][7]]
                    for r in f['events'] if r['event']=='velocity-commit']),
                (prefix+'_records_119', [[r['word'] for r in f['events']
                    if r['event']=='metadata-row' and r['parent']==i]
                    for i in range(f['records'])]),
            ]
            for name,expected in tables:
                table=source.split(name+'[][',1)[1].split('={',1)[1].split('};',1)[0]
                self.assertEqual([int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',table)],[w for row in expected for w in row])
            body=source.split(prefix+'_script_119[]=')[1]
            script=''.join(json.loads(line.strip().rstrip(';'))for line in body.splitlines()if line.strip().startswith('"'))
            original=(ROOT/'tools/frida'/f['probe']).read_text()
            self.assertEqual(script,original.replace("'hfoo'","'hT19'")+'function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n')

    def test_sources_and_map_embedding_certificate_are_content_addressed(self):
        for _,_,f,_ in self.scenes:
            sources={f['probe']:f['probe_sha256']}
            for cap in f['captures']:sources.update(cap['metadata']['source_sha256'])
            for name,digest in sources.items():
                if name=='map':continue
                raw=gzip.decompress((ROOT/f'tools/ghidra/fixtures/sources/{digest}.gz').read_bytes())
                self.assertEqual(hashlib.sha256(raw).hexdigest(),digest)
            self.assertEqual(hashlib.sha256((ROOT/'tools/frida'/f['probe']).read_bytes()).hexdigest(),f['probe_sha256'])
            self.assertTrue(f['probe_embedding']['verified'])
            self.assertEqual(f['probe_embedding']['map_sha256'],f['captures'][0]['metadata']['source_sha256']['map'])

    def test_saved_ghidra_names_types_abi_and_virtual_roots_match(self):
        f=json.loads((ROOT/'tools/ghidra/fixtures/retail-metadata-119-static.json').read_text())
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        self.assertFalse(f['unsaved']);self.assertEqual(len(f['functions']),13)
        for r in f['functions']:
            self.assertTrue(r['decompiled']);self.assertIn(r['name'],mapping);self.assertTrue(r['xrefs'])
            spec=next((x for x in schema['methods']if x['address']==r['address']),None)
            if spec:
                self.assertEqual([x['name']for x in r['parameters']],[x['name']for x in spec['parameters']])
        self.assertEqual([(l['name'],l['length'])for l in f['layouts']],[('WC3ModalAbilityPrefix',52),('WC3ModalEventPrefix',12)])
        reject=next(r for r in f['functions']if r['name']=='Ability_RejectOrderInterception')
        self.assertEqual(reject['returns'][0]['text'],'RET 0x4')

if __name__=='__main__':unittest.main()
