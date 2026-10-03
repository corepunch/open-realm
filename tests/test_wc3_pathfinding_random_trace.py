"""A complete native witness is required; damaged owner words cannot certify parity."""
import copy, json, sys, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_random_trace import check

class RandomWitnessTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-random-1.27.json').read_text())
        self.rows=[dict(event='metadata',sha256=self.fixture['binary_sha256']),
                   dict(event='marker',value='PATHTRACE tick=0 label=start_random_owner x=0'),
                   dict(event='marker',value='PATHTRACE tick=300 label=complete x=0'),
                   dict(event='trace-end',installed=True)]
        for sequence in self.fixture['sequences']:
            seed=sequence['seed'];prior=sequence['initial']
            self.rows.append(dict(event='random-native',case=f'seed_{seed}',native='SetRandomSeed',input=[seed],after=prior))
            for i,op in enumerate(sequence['operations'][:48]):
                native='GetRandomReal' if op['kind']==2 else 'GetRandomInt'
                self.rows.append(dict(event='random-native',case=f'seed_{seed}_op_{i}',native=native,
                    input=op['input'] if op['kind'] in (1,2) else [0,1],before=prior,after=op['state'],
                    output=op['output'][0] if op['kind'] in (1,2) else op['state'][0]>>31))
                prior=op['state']
    def run_rows(self,rows):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'trace.jsonl';path.write_text(''.join(json.dumps(r)+'\n' for r in rows))
            return check(path,self.fixture)
    def test_original_words_and_complete_scenario_are_accepted(self):
        self.assertEqual(len(self.run_rows(self.rows)),539)
    def test_wrong_state_input_output_count_and_completion_are_rejected(self):
        changed=[]
        for field in ('before','after','input'):
            rows=copy.deepcopy(self.rows);rows[5][field][0]^=1;changed.append(rows)
        rows=copy.deepcopy(self.rows);rows[5]['output']^=1;changed.append(rows)
        changed += [self.rows[:-1],self.rows[:2]+self.rows[3:],self.rows[:3]+self.rows[4:]]
        for rows in changed:
            with self.subTest(rows=len(rows)),self.assertRaises(ValueError):self.run_rows(rows)
    def test_observer_failure_and_wrong_binary_are_rejected(self):
        for failure in ('error','trace-failed'):
            with self.assertRaises(ValueError):self.run_rows(self.rows+[dict(event=failure)])
        rows=copy.deepcopy(self.rows);rows[0]['sha256']='0'*64
        with self.assertRaises(ValueError):self.run_rows(rows)

if __name__=='__main__':unittest.main()
