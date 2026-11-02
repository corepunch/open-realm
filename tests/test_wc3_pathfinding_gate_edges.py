"""Complete original portal searches/distances and controlled route consumers."""
import ctypes,json,struct,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class GateEdgeTests(unittest.TestCase):
    def test_native_special_routes_nodes_distances_and_consumers_at_o0_o2(self):
        original=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-edges-1.27.json').read_text())
        consumers=json.loads((ROOT/'tools/ghidra/fixtures/retail-gate-consumer-1.27.json').read_text())
        self.assertEqual(sum(len(r['searches'])for r in original['observations']),4608)
        self.assertEqual(len(consumers['cases']),2016)
        with tempfile.TemporaryDirectory(prefix='wc3-gate-edge-')as tmp:
            for opt in ('-O0','-O2'):
                path=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DBZ_WC3_FINE_TRACE',opt,'-fPIC','-shared','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(path)],check=True)
                engine=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32);u8=ctypes.POINTER(ctypes.c_uint8)
                engine.pathing_adaptive_special_route.argtypes=[u32,u8,u8,u32,u32]
                engine.pathing_adaptive_special_distance.argtypes=[u32,u8,u8,u32,u32]
                engine.pathing_adaptive_special_node_state.argtypes=[u32]
                engine.pathing_adaptive_gate_consumer.argtypes=[u32,u32,u32]
                out=(ctypes.c_uint32*(5+2*131072))();ds=(ctypes.c_uint32*6)()
                for row in original['observations']:
                    markers=(ctypes.c_uint8*1681)(*row['markers'])
                    lanes=[]
                    for lane in (0,2,4,6):
                        lanes.append((ctypes.c_uint8*2206)(*((v>>(6-lane))&3 for v in row['classbytes'])))
                    for lane,size,budget,warp,dx,dy,active,index in row['searches']:
                        records=(ctypes.c_uint32*(256*3))()
                        for identity in (1,2):records[identity*3:identity*3+3]=[active,dx,dy]
                        q=(ctypes.c_uint32*9)(41,41,size,budget,*struct.unpack('<4I',struct.pack('<4f',6.25,6.75,27.25,27.75)),warp)
                        wanted=original['expected'][index]
                        engine.pathing_adaptive_special_route(q,lanes[lane//2],markers,records,out)
                        expected=[wanted['result'],wanted['work'],wanted['nodes'],len(wanted['route_words'])//2,wanted['warp_count']]+wanted['route_words']
                        self.assertEqual(list(out[:len(expected)]),expected)
                        ns=(ctypes.c_uint32*(1+10*wanted['nodes']))();engine.pathing_adaptive_special_node_state(ns)
                        self.assertEqual(list(ns),[wanted['nodes']]+[v for node in wanted['node_state']for v in node])
                        engine.pathing_adaptive_special_distance(q,lanes[lane//2],markers,records,ds)
                        self.assertEqual(list(ds),[wanted['distance'],*wanted['distance_point'],wanted['distance_warps'],wanted['work'],wanted['nodes']])
                out=(ctypes.c_uint32*6)()
                for row in consumers['cases']:
                    q=(ctypes.c_uint32*5)(len(row['points'])//2,row['index'],row['execute'],row['active'],row['placement_result'])
                    words=(ctypes.c_uint32*len(row['points']))(*row['points'])
                    engine.pathing_adaptive_gate_consumer(q,words,out)
                    self.assertEqual(list(out),[row['result'],row['next_index'],row['warped'],len(row['placed']),*(row['placed'][0]if row['placed']else[0,0])])

if __name__=='__main__':unittest.main()
