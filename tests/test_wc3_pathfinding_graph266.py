"""Missing child ownership, leaked relations and fake reuse must reject parity."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_graph266 as v
class GraphTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_complete(self):self.assertEqual(len(v.validate(self.spec)['rows']),16)
 def test_missing_class(self):self.reject(lambda s:s.update(rows=s['rows'][:8]))
 def test_missing_reuse(self):self.reject(lambda s:s['rows'][0]['phases'].pop())
 def test_forward_child_release(self):self.reject(lambda s:s['rows'][0]['phases'][0].update(child_return_order=[5,6,7,8,0]))
 def test_borrowed_peer_destroyed(self):self.reject(lambda s:s['rows'][0]['phases'][0].update(borrowed_survivors=3))
 def test_duplicated_return(self):self.reject(lambda s:s['rows'][0]['phases'][0]['complete_return_order'].append(0))
 def test_leaked_child(self):self.reject(lambda s:s['rows'][0]['phases'][0].update(final_live=1))
 def test_pending_request(self):self.reject(lambda s:s['rows'][0]['phases'][0].update(zero_pending=False))
 def test_reuse_allocation(self):self.reject(lambda s:s['rows'][0]['phases'][1].update(factory_imports=1))
 def test_changed_source(self):self.reject(lambda s:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_wrong_game(self):self.reject(lambda s:s.update(game_sha256='0'*64))
if __name__=='__main__':unittest.main()
