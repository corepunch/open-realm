#!/usr/bin/env python3
"""Verify Captain roster/target event ownership against complete native cargo witnesses."""
import argparse,gzip,json,hashlib
from pathlib import Path
from verify_wc3_pathing_player_point214 import original_bytes
from wc3_pathfinding_engine_tests import run_engine
SPEC=Path(__file__).with_name('fixtures')/'retail-captain378-roles-1.27.json.gz'
PATTERNS=['wc3_save.target378_rejects_version_213_before_captain_role_masks','wc3_movement.target378_*','wc3_movement.cargo329_distant_attack_captain_recenters_on_passenger_return',
 'wc3_movement.public_captain_partial_removal_preserves_remaining_roster_and_save',
 'wc3_movement.public_captain_temporary_hero_removed_attack_ranges']
def validate(s):
 for path,pin in s['pins'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==pin,path
 assert s['payoff']==378 and s['task']=='GROUP-03.4.7.3'
 assert (s['captures'],s['controls'],len(s['markers']),len(s['records']),len(s['instructions']))==(2,1,678,1264,2287)
 assert s['records'][0]['event']=='marker' and ' label=start ' in s['records'][0]['value']
 assert s['records'][-1]['event']=='marker' and ' label=complete ' in s['records'][-1]['value']
 assert [r['value']for r in s['records']if r['event']=='marker']==s['markers']
 wisp=lambda r:r.get('state',{}).get('rawcode')==0x65777370
 subscriptions=[r for r in s['records']if r['event']=='captain-subscription' and wisp(r)]
 assert [(r['enabled'],r['lost'],r['death'],r['owner'],r['caller'])for r in subscriptions]==[(1,0,1,1,'9cf81e'),(0,1,1,1,'9d77e6'),(0,0,1,1,'9d57e3')]
 callbacks=[r for r in s['records']if r['event']=='captain-target-event-enter' and wisp(r)]
 assert len(callbacks)==1 and callbacks[0]['tick']==620 and callbacks[0]['count']==1
 recenter=[r for r in s['records']if r['event']=='cargo-captain-recenter' and wisp(r)]
 assert len(recenter)==1 and recenter[0]['tick']==579 and recenter[0]['result']==1
 assert any(r['event']=='cargo-target-lost' and wisp(r) and r['tick']==551 and r['caller']=='688373' for r in s['records'])
 assert [r['seq']for r in s['records']]==list(range(2,1266))
 assert s['observation_sequences']==[list(range(2,1266))]*2
 assert all(len(a)==1 and list(a.values())==[0]for a in s['captain_aliases'])
 assert all(r['captain']==0 for r in s['records']if 'captain'in r and not isinstance(r['captain'],list))
 return dict(captures=2,controls=1,public_markers=678,native_records=1264)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=Path('build/bin/openwarcraft3-tests'));p.add_argument('--data',type=Path,default=Path('build/tests'));a=p.parse_args()
 s=json.loads(gzip.decompress(SPEC.read_bytes()));metrics=validate(s);original_bytes(a.binary,s['instructions'])
 engine=run_engine(a.test_binary,a.data,a.report,PATTERNS);assert all(v==dict(tests=8,assertions=376)for v in engine.values()),engine
 report=dict(passed=True,status='verified',game_library_sha256=hashlib.sha256((a.test_binary.resolve().parent.parent/'lib/libgame-wc3-test.so').read_bytes()).hexdigest(),binary_sha256=s['binary_sha256'],instructions=len(s['instructions']),**metrics,engine=engine,scope=s['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
