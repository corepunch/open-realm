#!/usr/bin/env python3
"""Reproject complete archived ORDER-01.18 Patrol captures and their one control.

The two queued-input witnesses are distinct single runs, not matched repeats.
The no-input threshold/combat scene repeats its public words and decisions.
Engine saved FIFO composition is checked separately, not a retail UI save claim.
"""
import argparse,gzip,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_attack173_orders import check_capture,module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-01.18-expected.json.gz')
SHA='b619ea6c63fe6776d6e597a04e361ce480cd0867fdaf889b22aeb86fe5a26fbb'

def verify(archive):
 raw=gzip.decompress(EXPECTED.read_bytes());assert digest(raw)==SHA;expected=json.loads(raw)
 summarizer=module('patrol176_summary',Path('tools/frida/research/order0110_summarize.py'))
 claims=module('patrol176_claims',Path('tools/frida/research/order0118_verify.py'))
 assert expected['binary_sha256']==BINARY_SHA and not claims.check(expected)
 counts=dict(captures=0,observer_free_comparisons=0,records=0,markers=0)
 for name,scene in expected['scenes'].items():
  public=[];decisions=[];words=[]
  for frozen in scene['captures']:
   raw=(archive/'ORDER-01.18/captures'/Path(frozen['path']).name).read_bytes()
   stream,tasks=check_capture(raw,frozen,summarizer);public.append(stream);decisions.append(tasks);words.append(frozen['words_sha256'])
   counts['captures']+=1;counts['records']+=frozen['records'];counts['markers']+=frozen['markers']
  for frozen in scene['controls']:
   raw=(archive/'ORDER-01.18/captures'/Path(frozen['path']).name).read_bytes();assert digest(raw)==frozen['sha256']
   stream=re.findall(r'call Preload\( "((?:O110|O118) [^"\r\n]*)" \)',raw.decode())
   assert len(stream)==frozen['markers'] and digest('\n'.join(stream).encode())==frozen['public_sha256']
   public.append(stream);counts['observer_free_comparisons']+=1
  assert len(scene['captures'])==(2 if name=='p2'else 1)
  assert len(scene['controls'])==(1 if name=='p2'else 0)
  assert all(s==public[0]for s in public)==scene['public_identical']
  assert (len(set(words))==1)==scene['words_identical']
  assert all(s==decisions[0]for s in decisions)==scene['decisions_identical']
  assert summarizer.timelines(public[0])==scene['timelines']
  assert {k:v for k,v in decisions[0].items()if k!='factory'}==scene['decisions']
  assert decisions[0].get('factory',[])==scene['order_factories']
 return dict(passed=True,status='retail-patrol-leg-ownership',binary_sha256=BINARY_SHA,**counts,
  scope='Threshold, public combat head, same-leg resume, return append and queued rotation; actual engine native/frame/FIFO/save regressions.',
  exclusions=['Queued-input scenes are distinct single witnesses, not identical repeats.','No new live retail capture or retail UI save/load witness.','Unit-target Patrol, disabled Move gates and exact route/leg timing remain outside this point/combat composition.'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--binary',type=Path);a=p.parse_args()
 assert not a.output.exists()
 if a.binary:assert digest(a.binary.read_bytes())==BINARY_SHA
 report=verify(a.archive);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
