"""Original source-marker publication, complete routes and live-producer rejection."""
import copy,ctypes,hashlib,json,re,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_gate_markers_trace import expected,verify


def state_bytes(classbytes,markers):
    state=[];off=0
    for side in (41,20,10,5):
        n=side*side
        for lane in (0,2,4,6):state.extend((v>>(6-lane))&3 for v in classbytes[off:off+n])
        off+=n
    return state+markers


class GateMarkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-markers-1.27.json').read_text())
        cls.live=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-markers-live-1.27.json').read_text())

    def test_original_publications_and_all64_routes_nodes_at_o0_o2(self):
        with tempfile.TemporaryDirectory(prefix='wc3-gate-markers-')as tmp:
            for opt in ('-O0','-O2'):
                lib=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DBZ_WC3_FINE_TRACE',opt,'-fPIC','-shared','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(lib)],check=True)
                engine=ctypes.CDLL(str(lib));u32=ctypes.POINTER(ctypes.c_uint32);u8=ctypes.POINTER(ctypes.c_uint8)
                engine.pathing_waygate_publish.argtypes=[u32,ctypes.c_uint32,u8]
                engine.pathing_adaptive_route.argtypes=[u32,u8,u32];engine.pathing_adaptive_node_state.argtypes=[u32]
                for stage,row in enumerate(self.original['observations']):
                    if stage%4==0:state=(ctypes.c_uint8*10505)(*state_bytes(self.original['initial_classes'],[0]*1681))
                    q=(ctypes.c_uint32*8)(64,64,*struct.unpack('<4I',struct.pack('<4f',*row['box'])),0,0)
                    engine.pathing_waygate_publish(q,row['identity'],state)
                    self.assertEqual(list(state),state_bytes(row['classbytes'],row['markers']))
                    for query in row['searches']:
                        lane=query['lane']//2;classes=[];off=0
                        for side in (41,20,10,5):
                            n=side*side;classes.extend(state[off+lane*n:off+(lane+1)*n]);off+=4*n
                        out=(ctypes.c_uint32*(4+2*65536))();q=(ctypes.c_uint32*8)(41,41,query['size_input'],400,*struct.unpack('<4I',struct.pack('<4f',4.25,4.75,27.25,27.75)))
                        engine.pathing_adaptive_route(q,(ctypes.c_uint8*2206)(*classes),out)
                        wanted=[query['result'],query['work'],query['nodes'],len(query['route_words'])//2]+query['route_words']
                        self.assertEqual(list(out[:len(wanted)]),wanted)
                        ns=(ctypes.c_uint32*(1+query['nodes']*8))();engine.pathing_adaptive_node_state(ns)
                        self.assertEqual(list(ns),[query['nodes']]+[v for n in query['node_state']for v in n])

                for row in self.original['boundaries']:
                    state=(ctypes.c_uint8*10505)(*state_bytes(self.original['initial_classes'],[0]*1681))
                    for identity,box in ([(1,row['seed'])]if row['seed']else [])+[(row['identity'],row['box'])]:
                        q=(ctypes.c_uint32*8)(64,64,*struct.unpack('<4I',struct.pack('<4f',*box)),*struct.unpack('<2I',struct.pack('<2f',*row['origin'])))
                        engine.pathing_waygate_publish(q,identity,state)
                    self.assertEqual(list(state),state_bytes(row['classbytes'],row['markers']))

    def test_live_producer_requires_completion_and_every_parent_and_marker_byte(self):
        capture=self.live['captures'][0]
        rows=[dict(event='metadata',**capture['metadata'])]+expected(self.original)+[dict(event='marker',value=self.live['completion']),dict(event='trace-end',installed=True)]
        self.assertEqual(verify(rows,self.live,capture,self.original),expected(self.original))
        for change in ('end','complete','duplicate','source','marker','parent','rectangle','truncated','error'):
            bad=copy.deepcopy(rows)
            if change=='end':bad.pop()
            elif change=='complete':bad.pop(-2)
            elif change=='duplicate':bad.insert(-1,dict(event='marker',value=self.live['completion']))
            elif change=='source':bad[0]['source_sha256']['map']='0'*64
            elif change=='marker':bad[1]['maps'][0]['markers'][0]=2
            elif change=='parent':bad[1]['maps'][3]['classes'][0]^=1
            elif change=='rectangle':bad[1]['rectangle'][0]^=1
            elif change=='truncated':bad.pop(1)
            else:bad.append(dict(event='trace-failed'))
            with self.subTest(change=change),self.assertRaises(ValueError):verify(bad,self.live,capture,self.original)

    def test_production_route_fixture_is_the_original_complete_output(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_gate_overlap.h').read_text()
        for stage,row in enumerate(self.original['observations']):
            for k,query in enumerate(row['searches']):
                text=source.split(f'retail_gate_overlap_route_{stage}_{k}[][2]={{',1)[1].split('};',1)[0]
                self.assertEqual([int(v,16)for v in re.findall(r'0x([0-9a-f]+)u',text)],query['route_words'])

    def test_committed_overlap_jass_matches_the_archived_producer(self):
        raw=(ROOT/'tools/frida/wc3_waygate_overlap_probe.j').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.live['producer_sha256'])
        self.assertEqual(self.live['captures'][0]['metadata']['source_sha256']['wc3_waygate_capacity_probe.j'],self.live['producer_sha256'])
        self.assertEqual(hashlib.sha256((ROOT/'tools/ghidra/fixtures/retail-gate-markers-1.27.json').read_bytes()).hexdigest(),self.live['original_sha256'])


if __name__=='__main__':unittest.main()
