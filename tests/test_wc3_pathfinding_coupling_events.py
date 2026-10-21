import copy,gzip,json,re,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import coupling_events_evidence as v

class CouplingEventEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
        cls.data=v.unpack(cls.bundle)
    def rejected(self,edit,message):
        data=copy.deepcopy(self.data);edit(data)
        with self.assertRaisesRegex(ValueError,message):v.audit_loaded(data)
    def event(self,data,name,scene=0):
        return next(r for r in data['captures'][0]['rows'] if r['event']==name and r.get('scene')==scene)
    def public_change(self,data,scene,label,key,value,extra=''):
        for capture in data['captures']:
            raw=capture['preload'];markers=re.findall(rb'COUP [^"\r\n]+',raw)
            old=next(m for m in markers if f'scene={scene} '.encode() in m and f'label={label} '.encode() in m and extra.encode() in m)
            new=re.sub(rb'\b'+key.encode()+rb'=[^ ]+',key.encode()+b'='+value.encode(),old)
            capture['preload']=raw.replace(old,new)
            for row in capture['rows']:
                if row['event']=='marker' and row['value']==old.decode():row['value']=new.decode()
            capture['rows'][-1]['sha256']=v.sha(capture['preload'])
    def test_complete(self):
        result=v.audit();self.assertEqual(result['matching_public_markers'],711);self.assertFalse(result['engine_parity'])
    def test_independent_compressed_digest(self):
        with patch.object(Path,'read_bytes',return_value=b'invalid'):
            with self.assertRaisesRegex(ValueError,'compressed bundle digest'):v.audit()
    def test_reciprocal_target_role(self):
        self.rejected(lambda d:self.public_change(d,0,'issued-target','target','2','oid=852141'),'exact event sequence')
    def test_peer_stop_order(self):
        self.rejected(lambda d:self.public_change(d,0,'issued-immediate','actor','1','actor=2'),'exact event sequence')
    def test_all_issued_heads_are_pinned(self):
        self.rejected(lambda d:self.public_change(d,3,'issued-immediate','hh','0','actor=1'),'literal issued response context')
    def test_nested_reciprocal_move_survives(self):
        self.rejected(lambda d:self.public_change(d,2,'order-return','hh','852141'),'reciprocal replacement retained')
    def test_nested_peer_stop_move_survives(self):
        self.rejected(lambda d:self.public_change(d,3,'stop-archer','hh','0'),'peer callback replacement retained')
    def test_public_projection_digest(self):
        self.rejected(lambda d:self.public_change(d,1,'spawn','af','179.999'),'literal public digest')
    def test_task_opposing_actor(self):
        self.rejected(lambda d:self.event(d,'target-task').update(target='0x0'),'task incarnation/opposite')
    def test_task_actor_incarnation(self):
        self.rejected(lambda d:self.event(d,'target-task')['unit'].update(identity=[0,0]),'task incarnation/opposite')
    def test_task_caller(self):
        self.rejected(lambda d:self.event(d,'target-task').update(caller=0),'task raw range/caller')
    def test_zero_physical_range(self):
        self.rejected(lambda d:self.event(d,'target-task').update(range=0x43a68000),'task raw range/caller')
    def test_ready_partner_incarnation(self):
        self.rejected(lambda d:self.event(d,'couple-ready',2).update(partner=[0,0]),'ready identity/range')
    def test_ready_authored_range(self):
        self.rejected(lambda d:self.event(d,'couple-ready',2).update(range=0),'ready identity/range')
    def test_authored_map_override(self):
        self.rejected(lambda d:d.update(w3a=d['w3a'][:-1]),'authored333')
    def test_footer(self):
        self.rejected(lambda d:d['captures'][0]['rows'][-1].update(complete=False),'footer')
    def test_control_is_unhooked(self):
        self.rejected(lambda d:d['captures'][2]['rows'].insert(1,{'event':'target-task'}),'unhooked control')
    def test_trace_counts(self):
        self.rejected(lambda d:next(r for r in d['captures'][0]['rows'] if r['event']=='trace-end')['counts'].update(marker=1),'counts')
    def test_capture_source_pins(self):
        self.rejected(lambda d:d['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64),'source pins')
    def test_engine_parity_claim(self):
        bundle=copy.deepcopy(self.bundle);bundle['engine_parity']=True
        with self.assertRaisesRegex(ValueError,'evidence contract'):v.audit(bundle)

if __name__=='__main__':unittest.main()
