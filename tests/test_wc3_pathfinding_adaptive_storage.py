"""Complete original adaptive nodes/routes/growth, public results and retained reset."""
import copy,ctypes,hashlib,json,re,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_adaptive_storage_trace import verify

class AdaptiveStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-storage-1.27.json').read_text())
        cls.live=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-storage-live-1.27.json').read_text())

    def test_complete_original_requests_nodes_and_heap_prefix_at_o0_o2(self):
        levels=[[int(x==384 or(x&1 and y&1))for y in range(512)for x in range(512)]]
        for side in (512,256,128):
            old=levels[-1];level=[]
            for y in range(side//2):
                for x in range(side//2):
                    children=[old[(2*y+dy)*side+2*x+dx]for dy in (0,1)for dx in (0,1)]
                    level.append(0 if all(v==0 for v in children)else 1 if all(v==1 for v in children)else 2)
            levels.append(level)
        cells=(ctypes.c_uint8*sum(map(len,levels)))(*(v for l in levels for v in l))
        words=struct.unpack('<4I',struct.pack('<4f',4.25,4.75,448.25,400.75))
        with tempfile.TemporaryDirectory(prefix='wc3-adaptive-storage-')as tmp:
            for opt in ('-O0','-O2'):
                path=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DBZ_WC3_FINE_TRACE',opt,'-fPIC','-shared','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(path)],check=True)
                engine=ctypes.CDLL(str(path));u32=ctypes.POINTER(ctypes.c_uint32)
                engine.pathing_adaptive_route.argtypes=[u32,ctypes.POINTER(ctypes.c_uint8),u32]
                engine.pathing_adaptive_storage.argtypes=[ctypes.c_uint32,u32]
                engine.pathing_adaptive_node_state.argtypes=[u32]
                engine.pathing_adaptive_heap_prefix.argtypes=[ctypes.c_uint32,u32]
                for row in self.fixture['records']+self.fixture['public']:
                    with self.subTest(opt=opt,budget=row['budget']):
                        out=(ctypes.c_uint32*(4+2*65536))();q=(ctypes.c_uint32*8)(512,512,0,row['budget'],*words)
                        engine.pathing_adaptive_route(q,cells,out)
                        self.assertEqual(list(out[:4+len(row['route_words'])]),[row['result'],row['work'],row['nodes'],row['route_count']]+row['route_words'])
                        state=(ctypes.c_uint32*(1+8*row['nodes']))();engine.pathing_adaptive_node_state(state)
                        normalized=[list(state[1+8*i:9+8*i])for i in range(state[0])]
                        self.assertEqual(hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest(),row['node_sha256'])
                        cap=(ctypes.c_uint32*3)();engine.pathing_adaptive_storage(0,cap)
                        self.assertEqual(list(cap),[row['node_capacity'],row['heap_capacity'],row['nodes']])
                        if 'index'in row:
                            self.assertEqual(row['admitted'],1);self.assertEqual(row['index'],row['route_count']-1)
                            self.assertEqual(bool(row['flags']&0x20000000),not row['result'])
                out=(ctypes.c_uint32*(1+3*4096))();engine.pathing_adaptive_heap_prefix(4096,out)
                control=self.fixture['heap_control'];self.assertEqual(out[0],control['capacity'])
                self.assertEqual(hashlib.sha256(struct.pack('<'+ 'I'*control['words'],*out[1:])).hexdigest(),control['sha256'])
                recovery=self.fixture['recovery'];result=(ctypes.c_uint32*(4+2*65536))()
                engine.pathing_adaptive_route((ctypes.c_uint32*8)(512,512,0,400,*words),cells,result)
                self.assertEqual(list(result[:4+len(recovery['route_words'])]),[0,recovery['work'],recovery['nodes'],len(recovery['route_words'])//2]+recovery['route_words'])
                cap=(ctypes.c_uint32*3)();engine.pathing_adaptive_storage(1,cap);self.assertEqual(list(cap),[0,0,0])

    def test_ushort_alias_and_public_partial_are_explicit(self):
        self.assertEqual([(r['identity'],r['stored_index'],r['lookup'])for r in self.fixture['ushort_alias']],[(65535,65535,65535),(65536,0,0),(65537,1,1)])
        self.assertEqual(self.fixture['records'][-1]['nodes'],65538)
        self.assertEqual(self.fixture['records'][-1]['result'],1)
        self.assertEqual([(r['slots'],r['capacity'])for r in self.fixture['heap_control']['prefix']],[(2048,2048),(2049,4096),(4096,4096),(4097,6144)])

    def test_production_fixture_preserves_route_words_and_nodes(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_adaptive_storage.h').read_text()
        rows=[next(r for r in self.fixture['records']if r['budget']==b)for b in (5000,400)]
        for i,row in enumerate(rows):
            table=source.split(f'retail_acc_storage_route_{i}[][2]={{',1)[1].split('};',1)[0]
            self.assertEqual([int(v,16)for v in re.findall(r'0x([0-9a-f]+)u',table)],row['route_words'])
        self.assertEqual(re.findall(r'UINT64_C\(0x([0-9a-f]+)\)',source),[r['node_fnv64']for r in rows])

    def test_live_constructor_growth_rejects_bad_identity_and_provenance(self):
        capture=self.live['captures'][0];rows=[dict(event='metadata',**capture['metadata'])]
        for observed in self.live['observations']:
            row=copy.deepcopy(observed)
            if row['event']=='adaptive-storage-constructed':row['search']='0x10000000'
            else:row['table']='0x10000050'if row['kind']=='nodes'else '0x10000070'
            rows.append(row)
        rows.append(dict(event='trace-end',installed=True))
        self.assertEqual(verify(rows,self.live,capture),self.live['observations'])
        for change in ('table','capacity','source','end'):
            bad=copy.deepcopy(rows)
            if change=='table':bad[1]['table']='0x10000044'
            elif change=='capacity':bad[2]['index']['capacity']=255
            elif change=='source':bad[0]['source_sha256']['wc3_pathfinding.js']='0'*64
            else:bad.pop()
            with self.subTest(change=change),self.assertRaises(ValueError):verify(bad,self.live,capture)

if __name__=='__main__':unittest.main()
