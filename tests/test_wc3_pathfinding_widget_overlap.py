"""Incomplete or stale authored-widget captures must not close MAP-02.3."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_widget_overlap_trace import verify_overlap


class WidgetOverlapTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-widget-overlap-1.27.json').read_text())
        self.rows=[]
        for life,state in zip(self.fixture['life_markers'],self.fixture['states'],strict=True):
            self.rows.append(dict(event='blocker-lifecycle-marker',value=life))
            self.rows.append(dict(event='blocker-geometry',marker=life,**copy.deepcopy(self.fixture['geometry'][state])))
        for i in range(8):
            self.rows.append(dict(event='widget-method',method='destroy',beforeCollection='0x1' if i<6 else '0x0',afterCollection='0x0'))
        self.rows.append(dict(event='marker',value='PATHTRACE label=complete x=0'))
        self.rows.append(dict(event='trace-end',installed=True,counts={'widget-destroy':8}))

    def test_complete_both_creation_orders(self):
        self.assertEqual(verify_overlap(self.rows,self.fixture)['snapshots'],9)

    def test_missing_reverse_creation_order(self):
        rows=[r for r in self.rows if 'label=gate_then_tree ' not in r.get('marker',r.get('value',''))]
        with self.assertRaisesRegex(ValueError,'creation/removal order'):
            verify_overlap(rows,self.fixture)

    def test_wrong_public_pose(self):
        self.rows[2]['value']=self.rows[2]['value'].replace('-1920.000','-1936.000')
        with self.assertRaisesRegex(ValueError,'public pose'):
            verify_overlap(self.rows,self.fixture)

    def test_stale_fine_or_any_parent_level(self):
        for field in ['masks',*range(4)]:
            rows=copy.deepcopy(self.rows)
            row=next(r for r in rows if r['event']=='blocker-geometry' and 'label=tree_then_gate ' in r['marker'])
            if field=='masks':row['masks'][0]^=2
            else:row['hierarchy'][field]['values'][0][0]^=1
            with self.assertRaisesRegex(ValueError,'footprint or hierarchy'):
                verify_overlap(rows,self.fixture)

    def test_incomplete_collection_retirement(self):
        next(r for r in self.rows if r['event']=='widget-method')['afterCollection']='0x1'
        with self.assertRaisesRegex(ValueError,'collection retirement'):
            verify_overlap(self.rows,self.fixture)

    def test_unfinished_or_failed_capture(self):
        for rows in (self.rows[:-1], self.rows+[dict(event='error')]):
            with self.assertRaisesRegex(ValueError,'observer failed'):
                verify_overlap(rows,self.fixture)

    def test_engine_literal_grids_equal_frozen_native_cells(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_widget_overlap.h').read_text()
        for name,key in [('retail_widget_masks','masks'),('retail_widget_classes','hierarchy')]:
            table=source.split(name,1)[1].split('={',1)[1].split('};',1)[0]
            table=re.sub(r'/\*.*?\*/','',table,flags=re.S)
            actual=[int(n) for n in re.findall(r'\d+',table)]
            expected=[]
            for state in ('baseline','tree','gate','both'):
                value=self.fixture['geometry'][state][key]
                expected+=value if key=='masks' else [v for h in value for cell in h['values'] for v in cell]
            self.assertEqual(actual,expected)


if __name__=='__main__':unittest.main()
