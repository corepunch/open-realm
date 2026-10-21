import base64,copy,gzip,json,re,unittest
from collections import Counter
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import militia_lifecycle_evidence as v
class LifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
        cls.markers=re.findall(r'call Preload\( "(MIL [^"\r\n]*)" \)',v.literal(cls.bundle['captures'][2],'preload').decode())
    def public_change(self,scene,label,key,value):
        rows=list(self.markers)
        for i,r in enumerate(rows):
            if f'scene={scene} ' in r and f'label={label} ' in r:
                rows[i]=re.sub(r'\b'+key+r'=[^ ]+',key+'='+value,r);return rows
        raise AssertionError('missing mutation target')
    def check_public(self,error,scene,label,key,value):
        with self.assertRaisesRegex(ValueError,error):v.public_contract(self.public_change(scene,label,key,value))
    def change(self,edit,index=0):
        d=copy.deepcopy(self.bundle);c=d['captures'][index]
        rows=[json.loads(s) for s in v.literal(c).splitlines()];edit(rows)
        for r in rows:
            if r['event']=='trace-end':r['counts']=dict(Counter(x['event'] for x in rows if x['event'] in r['counts']))
        raw=b''.join((json.dumps(r)+'\n').encode() for r in rows);c.update(raw_b64=base64.b64encode(raw).decode(),raw_sha256=v.digest(raw));return d
    def test_complete(self):self.assertEqual(v.audit()['public_markers'],1470)
    def test_admission(self):self.check_public('admission',0,'order-return','accepted','0')
    def test_stop(self):self.check_public('Stop ownership',1,'stop-return','head','852071')
    def test_move(self):self.check_public('replacement Move',2,'move-return','head','852071')
    def test_hall_retirement(self):self.check_public('Hall removal synchronous',3,'remove-hall','head','0')
    def test_target_ability(self):self.check_public('ability-loss admission',4,'remove-amic','head','0')
    def test_source_ability(self):self.check_public('ability-loss admission',5,'remove-amil','head','0')
    def test_pause(self):self.check_public('pause position',6,'before-resume','x','800.000')
    def test_resume_converts(self):self.check_public('final conversion',6,'settled','bmil','0')
    def test_stop_during_facing(self):self.check_public('stopped position',7,'settled','x','1000.000')
    def test_move_during_facing(self):self.check_public('replacement Move',8,'move-return','accepted','0')
    def test_physical_range(self):
        def edit(rows):next(r for r in rows if r['event']=='target-task' and r.get('scene')==7 and r.get('caller')==0x62e9ce)['range']=v.RANGE
        with self.assertRaisesRegex(ValueError,'physical owner'):v.audit(self.change(edit))
    def test_footer(self):
        with self.assertRaisesRegex(ValueError,'footer'):v.audit(self.change(lambda r:r[-1].update(complete=False)))
    def test_control_hooks(self):
        with self.assertRaisesRegex(ValueError,'control contains'):v.audit(self.change(lambda r:r.insert(1,{'event':'target-task'}),index=2))
    def test_source(self):
        with self.assertRaisesRegex(ValueError,'source pins'):v.audit(self.change(lambda r:r[0]['source_sha256'].update(map='0'*64)))
    def test_engine_claim(self):
        d=copy.deepcopy(self.bundle);d['engine_parity']=True
        with self.assertRaisesRegex(ValueError,'contract'):v.audit(d)
    def test_missing_complete(self):
        with self.assertRaisesRegex(ValueError,'completion'):v.public_contract(self.markers[:-1])
if __name__=='__main__':unittest.main()
