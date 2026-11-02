#!/usr/bin/env python3
"""Verify native mixed Move/Attack/Captain FIFO transactions and production ownership."""
import argparse,gzip,json,sys
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,public,original_bytes
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
import target363_trace as trace
SPEC=ROOT/'tools/ghidra/fixtures/retail-target363-1.27.json'
TESTS=['wc3_bot.target363_public_source_change_restores_captain_synchronously',
       'wc3_movement.target363_distinct_event_ranks_and_duplicate_updates_survive_save',
       'wc3_movement.target363_nested_growth_cancels_reused_outer_registration',
       'wc3_save.target363_rejects_duplicate_event_unknown_event_and_stale_incarnation',
       'wc3_movement.target167*','wc3_movement.target223*',
       'wc3_save.load_restores_server_clock_onto_saved_time',
       'wc3_bot.captain_near_home_hostile_contact*',
       'wc3_bot.captain_contact_preserves_negative*']
CLASSES={0xb62794:('CAbilityMove',0x5fda10),0xadb5a4:('CAbilityAttack',0x49a5f0),
         0xbe0000:('CCaptainAI',0x9d4aa0),0xb28330:('CAbilityInventory',0x561680),
         0xaddfb8:('CMissile',0x6d4e30)}

def verify_classes(binary):
 sys.path.insert(0,str(ROOT/'tools'))
 from extract_wc3_ability_classes import PE,u32
 data=binary.read_bytes();pe=PE(data)
 if digest(data)!=SHA:raise ValueError('unsupported original DLL')
 for rva,(name,handler)in CLASSES.items():
  va=pe.base+rva;col=u32(data,pe.offset(va-4));off=pe.offset(col)
  descriptor=u32(data,off+12);at=pe.offset(descriptor)+8
  expected=('.?AV'+name+'@@\0').encode()
  if(u32(data,off+4)!=0 or data[at:at+len(expected)]!=expected or
     u32(data,pe.offset(va)+12)!=pe.base+handler):raise ValueError('original callback RTTI/entry differs '+name)
 if u32(data,pe.offset(pe.base+0xaddfb8)+0xb8)!=pe.base+0x6d6f80:raise ValueError('Missile target inverse differs')
 if u32(data,pe.offset(pe.base+0xa9de14))!=pe.base+0x247520:raise ValueError('FloatMini point metadata differs')
 return len(CLASSES)

def runtime(groups,spec):
 if set(groups)!=set(trace.EVENTS):raise ValueError('missing public mutation variant')
 records=0
 for variant in trace.EVENTS:
  expected=spec['scenes'][variant];cases=groups[variant]
  if len(cases)!=3:raise ValueError('missing independent repeat/control')
  for index,case in enumerate(cases):
   rows=trace.validate_rows(case['rows']);meta=rows[0];mode='control'if index==2 else'observe'
   if(meta.get('mode')!=mode or meta.get('launcher')!='owned-wine-popen'or
      meta.get('env')not in('B','C')or meta.get('display')!={'B':':98','C':':99'}[meta['env']]or
      meta.get('remote')!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']]or
      meta.get('prefix')!='RSH 'or meta.get('input')or meta.get('frida')!='17.18.0'or
      meta.get('source_sha256')!=expected['sources'][index]or
      meta['source_sha256']['map']!=expected['map_sha256']):raise ValueError('capture provenance differs')
   markers=public(case['preload'])
   if markers!=expected['projection']['public_markers']:raise ValueError('unhooked/public stream differs')
   footer={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':302,'complete':True}
   if rows[-1]!=footer:raise ValueError('incomplete capture/preload binding')
   if mode=='observe':
    if trace.project(rows,variant)!=expected['projection']:raise ValueError('whole synchronous native transaction differs')
   elif any(r.get('event')in('module','trace-end','marker','snapshot','captain-call')for r in rows):raise ValueError('control contains instrumentation')
  records+=len(expected['projection']['transaction'])
 if records!=438:raise ValueError('native transaction coverage changed')
 return dict(observed_captures=6,controls=3,public_markers=2718,native_records=records)

def validate(spec):
 if(spec.get('version')!=1 or spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or
    spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=1233):raise ValueError('wrong task/tests/original guard inventory')
 for name,expected in spec['pins'].items():
  if digest((ROOT/name).read_bytes())!=expected:raise ValueError('source changed '+name)
 packed=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(packed)!=spec['bundle_sha256']:raise ValueError('frozen capture bundle changed')
 groups=json.loads(gzip.decompress(packed));runtime(groups,spec);return groups

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());groups=validate(spec);original_bytes(a.binary,spec['instructions']);classes=verify_classes(a.binary)
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=1233,callback_classes=classes,
             **runtime(groups,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
