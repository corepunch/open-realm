"""Frozen public pair transport proves phase words and rejects incomplete witnesses."""
import copy
import json
from pathlib import Path
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_public_pair_trace import verify_pair, canonical


def capture_rows(fixture,movers,marker):
    rows=[]
    for row in copy.deepcopy(fixture['phases']):
        if row['event'].startswith('pair-group-phase-'):
            for member in row['members']:
                member['mover']=movers[member.pop('member')]
                member['row'][5]=int(member['mover'],16)
        else:
            row['mover']=movers[row.pop('member')]
        rows.append(row)
    rows.extend(dict(event=marker,value=s) for s in fixture['markers'])
    return rows


class PublicPairTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-pair-1.27.json').read_text())
        self.rows=capture_rows(self.fixture,['0x1000','0x2000'],'pair-marker')

    def test_complete_original_phases_and_public_samples(self):
        result=verify_pair(self.rows,self.fixture)
        self.assertEqual(result['group_owner_passes'],60)
        self.assertEqual(result['member_commits'],[60,55])
        self.assertEqual(result['phase_sha256'],'f1c52abbf5213a2b22d6d804da6d92478deed97f8fcd7b344ab7dc9e9f2ecb83')

    def test_raw_words_order_members_and_samples_cannot_change(self):
        for kind in ('pose','member','flags','destination','velocity','phase','missing','sample'):
            with self.subTest(kind=kind):
                rows=copy.deepcopy(self.rows)
                phase=next(r for r in rows if r['event']=='pair-group-phase-end' and r['phase']=='decide')
                if kind=='pose': phase['members'][0]['pose'][2]^=1
                elif kind=='member': phase['members'][0]['mover']='0x2000'
                elif kind=='flags': phase['members'][0]['row'][10]^=1
                elif kind=='destination': phase['members'][0]['row'][6]^=1
                elif kind=='velocity': next(r for r in rows if r['event']=='velocity-commit')['after'][4]^=1
                elif kind=='phase': phase['phase']='commit'
                elif kind=='missing': rows.remove(phase)
                else: next(r for r in rows if r['event']=='pair-marker')['value']='PATHPAIR tick=10 accepted=0'
                with self.assertRaises(ValueError): verify_pair(rows,self.fixture)

    def test_engine_fixture_contains_every_original_commit_in_order(self):
        text=(ROOT/'games/warcraft-3/game/tests/retail_public_pair.h').read_text()
        text=text.split('public_pair_motion[][7]={',1)[1].split('};',1)[0]
        words=[[int(v.rstrip('u'),16) for v in re.findall(r'0x[0-9a-f]+u',line)]
               for line in text.splitlines() if line.lstrip().startswith('{0x')]
        self.assertEqual(words,self.fixture['engine_motion'])
        phases=[r for r in self.fixture['phases'] if r['event']=='pair-group-phase-begin' and r['phase']=='decide']
        header=(ROOT/'games/warcraft-3/game/tests/retail_public_pair.h').read_text()
        group=header.split('public_pair_group_before[60][6]={',1)[1].split('};',1)[0]
        actual=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',group)]
        expected=[v for r in phases for v in [r['flags'],r['age'],r['completion'],len(r['members']),*r['formation']]]
        self.assertEqual(actual,expected)
        members=header.split('public_pair_members_before[60][2][8]={',1)[1].split('};',1)[0]
        actual=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',members)]
        expected=[]
        for r in phases:
            for i in range(2):
                m=next((m for m in r['members'] if m['member']==i),None)
                expected.extend([*m['row'][3:5],*m['row'][6:11],int(bool(m['moverFlags']&0x10000))] if m else [0]*8)
        self.assertEqual(actual,expected)



if __name__=='__main__': unittest.main()
