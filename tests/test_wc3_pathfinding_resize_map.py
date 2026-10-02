"""Retail resize producers preserve map data and use collision/type metadata."""
import struct
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/frida'))
from make_wc3_pathfinding_map import resize_units,resize_ability

class ResizeMapTests(unittest.TestCase):
    def test_existing_tables_are_preserved_and_collision_is_unreal(self):
        original=b'hfoo'+b'\0'*4+struct.pack('<I',1)+b'unam'+struct.pack('<I',3)+b'original\0'+b'\0'*4
        custom=b'hpea'+b'hOLD'+struct.pack('<I',1)+b'umvs'+struct.pack('<If',1,271.25)+b'hOLD'
        source=struct.pack('<II',1,1)+original+struct.pack('<I',1)+custom
        result=resize_units(source)
        prefix=struct.pack('<II',1,1)+original+struct.pack('<I',3)+custom
        self.assertEqual(result[:len(prefix)],prefix)
        self.assertEqual(result[len(prefix):],b''.join(b'hfoo'+code+struct.pack('<I',1)+b'ucol'+struct.pack('<If',2,radius)+code for code,radius in [(b'hCLG',63.0),(b'hCLS',7.0)]))
    def test_existing_clone_and_trailing_data_are_rejected(self):
        source=struct.pack('<III',1,0,0)
        with self.assertRaises(ValueError):resize_units(source+b'bad')
        with self.assertRaises(ValueError):resize_units(resize_units(source))
    def test_chaos_target_uses_level_one_unit_id_and_clears_requirements(self):
        for scenario,code,target in [('follow_target_grow',b'ACGb',b'hCLG'),('follow_target_shrink',b'ACSh',b'hCLS')]:
            expected=struct.pack('<III',2,0,1)+b'Sca1'+code+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code+b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
            self.assertEqual(resize_ability(scenario),expected)
    def test_research_control_inherits_original_requires(self):
        expected=struct.pack('<III',2,0,1)+b'Sca1ACGb'+struct.pack('<I',1)+b'Cha1'+struct.pack('<III',3,1,0)+b'hCLG\0ACGb'
        self.assertEqual(resize_ability('follow_target_resize_gate'),expected)
if __name__=='__main__':unittest.main()
