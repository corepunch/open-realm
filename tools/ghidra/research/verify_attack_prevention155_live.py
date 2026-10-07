#!/usr/bin/env python3
"""Verify completed public buff/counter repeats and observer-free markers.

State/counter words are exact. Buff object flags are not normalized, because
unrelated allocation/presentation flags are outside this contract.
"""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def normalize(rows):
 return {event:[{k:v for k,v in r.items() if k!='flags'} for r in rows if r.get('event')==event]
         for event in ('marker','state','suppression','spells','buff')}

def read_capture(path,mode,expected):
 rows=[json.loads(line)for line in path.read_bytes().splitlines()]
 if not rows or sum(r.get('event')=='metadata'for r in rows)!=1 or rows[0].get('event')!='metadata':raise ValueError('missing/duplicated metadata')
 meta=rows[0]
 if meta.get('mode')!=mode or meta.get('sha256')!=HASH or meta.get('task')!='captain-prevention155':raise ValueError('wrong capture identity')
 if meta['source_sha256']['map']!=expected['map_sha256']:raise ValueError('wrong map')
 for name,digest in meta['source_sha256'].items():
  if name!='map' and hashlib.sha256((ROOT/'tools/frida/research'/name).read_bytes()).hexdigest()!=digest:raise ValueError('changed observer/controller')
 if any(r.get('type')=='error' or r.get('event') in ('trace-failed','error')for r in rows):raise ValueError('capture error')
 preload=[r for r in rows if r.get('event')=='preload-file']
 if len(preload)!=1 or preload[0].get('complete') is not True:raise ValueError('incomplete public producer')
 data=path.with_name(path.stem+'-preload.txt').read_bytes()
 if hashlib.sha256(data).hexdigest()!=preload[0]['sha256']:raise ValueError('changed public file')
 markers=re.findall(r'call Preload\( "(RSG [^"\r\n]*)" \)',data.decode())
 if len(markers)!=100 or markers[-1]!='RSG tick=280 label=complete':raise ValueError('missing public events')
 ends=[r for r in rows if r.get('event')=='trace-end']
 if mode=='observe':
  if len(ends)!=1 or ends[0].get('installed') is not True or ends[0]['counts']!={'apply':10,'suppression':16,'remove':9,'spells':3}:raise ValueError('incomplete observer')
  if [r['value'] for r in rows if r.get('event')=='marker']!=markers:raise ValueError('observer/public mismatch')
 elif ends or any(r.get('event')in ('marker','state','suppression','spells','buff')for r in rows):raise ValueError('control contains observer')
 if hashlib.sha256(path.read_bytes()).hexdigest()!=expected['captures'][path.name]:raise ValueError('capture pin differs')
 return rows,markers

def verify(captures,expected):
 frozen=json.loads(expected.read_text())
 if frozen.get('binary_sha256')!=HASH:raise ValueError('wrong frozen identity')
 for name,digest in frozen['sources'].items():
  if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise ValueError('changed producer source')
 public=None
 for name in ('v2-observe-2.jsonl','v2-observe-3.jsonl','v2-control-1.jsonl'):
  mode='control'if 'control' in name else 'observe'
  rows,markers=read_capture(captures/name,mode,frozen)
  if public is not None and markers!=public:raise ValueError('public repeat/control differs')
  public=markers
  if mode=='observe' and normalize(rows)!=frozen['normalized']:raise ValueError('counter/state repeat differs')
 return dict(passed=True,repeats=2,public_markers=100,states=290,counter_changes=16,spell_changes=3,
             exclusions=['full attack cancellation/dispatch','public Attack recreation (native rejected)','whole movement trajectory'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--captures',type=Path,required=True);p.add_argument('--expected',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('report must be fresh')
 try:result=verify(a.captures,a.expected)
 except (ValueError,KeyError,OSError)as error:result=dict(passed=False,error=str(error))
 a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));return 0 if result['passed']else 1
if __name__=='__main__':raise SystemExit(main())
