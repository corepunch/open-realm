import copy,gzip,json,re,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import coupling_original_actor_evidence as v

class OriginalActorEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()));cls.data=v.unpack(cls.bundle)
    def rejected(self,edit,message):
        data=copy.deepcopy(self.data);edit(data)
        with self.assertRaisesRegex(ValueError,message):v.audit_loaded(data)
    def event(self,data,name):return next(r for r in data['captures'][0]['rows'] if r['event']==name)
    def public_change(self,data,scene,label,key,value,extra=''):
        for capture in data['captures']:
            raw=capture['preload'];markers=re.findall(rb'COUP [^"\r\n]+',raw)
            old=next(m for m in markers if f'scene={scene} '.encode() in m and f'label={label} '.encode() in m and extra.encode() in m)
            new=re.sub(rb'\b'+key.encode()+rb'=[^ ]+',key.encode()+b'='+value.encode(),old)
            capture['preload']=raw.replace(old,new)
            for row in capture['rows']:
                if row['event']=='marker' and row['value']==old.decode():row['value']=new.decode()
            capture['rows'][-1]['sha256']=v.sha(capture['preload'])
    def test_complete(self):self.assertEqual(v.audit()['matching_public_markers'],366)
    def test_compressed_pin(self):
        with patch.object(Path,'read_bytes',return_value=b'changed'):
            with self.assertRaisesRegex(ValueError,'compressed bundle digest'):v.audit()
    def test_nested_move_is_accepted(self):
        self.rejected(lambda d:self.public_change(d,0,'nested-original-move-return','ah','0'),'nested original Move accepted')
    def test_outer_stop_supersedes_original_move(self):
        self.rejected(lambda d:self.public_change(d,0,'stop-archer','ah','851986'),'outer Stop wins')
    def test_outer_stop_event_after_nested_move(self):
        self.rejected(lambda d:self.public_change(d,0,'issued-immediate','ah','851986','actor=1'),'outer Stop published')
    def test_original_target_of_nested_point(self):
        self.rejected(lambda d:self.public_change(d,0,'issued-point','actor','2'),'exact event sequence')
    def test_remove_attempt_does_not_certify_removal(self):
        self.rejected(lambda d:self.public_change(d,1,'settled','at','0'),'removal attempt does not prove removal')
    def test_original_life_observation(self):
        self.rejected(lambda d:self.public_change(d,1,'settled','al','0.000'),'removal attempt does not prove removal')
    def test_no_original_move_after_outer_stop(self):
        self.rejected(lambda d:self.public_change(d,0,'settled','ax','512.000'),'no post-Stop travel')
    def test_all_issued_heads(self):
        self.rejected(lambda d:self.public_change(d,1,'issued-immediate','hh','0','actor=2'),'literal issued response context')
    def test_complete_public_digest(self):
        self.rejected(lambda d:self.public_change(d,1,'spawn','af','179.999'),'literal public digest')
    def test_opposite_target(self):
        self.rejected(lambda d:self.event(d,'target-task').update(target='0x0'),'task incarnation/opposite')
    def test_actor_incarnation(self):
        self.rejected(lambda d:self.event(d,'target-task')['unit'].update(identity=[0,0]),'task incarnation/opposite')
    def test_zero_range(self):
        self.rejected(lambda d:self.event(d,'target-task').update(range=0x43a68000),'task raw range/caller')
    def test_authored_override(self):
        self.rejected(lambda d:d.update(w3a=d['w3a'][:-1]),'authored333')
    def test_source_pins(self):
        self.rejected(lambda d:d['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64),'source pins')
    def test_complete_footer(self):
        self.rejected(lambda d:d['captures'][0]['rows'][-1].update(complete=False),'footer')
    def test_unhooked_control(self):
        self.rejected(lambda d:d['captures'][2]['rows'].insert(1,{'event':'target-task'}),'unhooked control')
    def test_trace_counts(self):
        self.rejected(lambda d:self.event(d,'trace-end')['counts'].update(marker=1),'counts')
    def test_no_engine_parity_claim(self):
        bundle=copy.deepcopy(self.bundle);bundle['engine_parity']=True
        with self.assertRaisesRegex(ValueError,'evidence contract'):v.audit(bundle)

if __name__=='__main__':unittest.main()
