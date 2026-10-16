#!/usr/bin/env python3
"""Reverse the two initial public casts in a frozen finite modifiers316 arena."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();assert not a.output.exists()
tool=str(ROOT/'build/bin/mpqtool');members=subprocess.check_output([tool,'-mpq',str(a.base),'ls'],text=True).splitlines()
with tempfile.TemporaryDirectory(prefix='modifiers316-reverse-')as d:
 temp=Path(d);payload=temp/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack'];script_hash=None
 for i,name in enumerate(members):
  if name=='(listfile)':continue
  data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
  if name=='war3map.j':
   text=data.decode();first='call IssueTargetOrder(udg_M316Slow,"slow",udg_M316Unit)';second='call IssueTargetOrder(udg_M316Haste,"bloodlust",udg_M316Unit)'
   assert first in text and second in text
   text=text.replace(first,'CALL_FIRST_CAST_316',1).replace(second,first,1).replace('CALL_FIRST_CAST_316',second,1)
   text=text.replace('M316Mark("slow-request")','M316Mark("haste-first")',1).replace('M316Mark("haste-request")','M316Mark("slow-next")',1)
   data=text.encode();script_hash=hashlib.sha256(data).hexdigest()
  f=temp/str(i);f.write_bytes(data);cmd.extend([str(f),name])
 subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
 a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
a.output.with_suffix('.json').write_text(json.dumps({'task':'NUM-03.2','variant':'reverse initial casts','base_sha256':hashlib.sha256(a.base.read_bytes()).hexdigest(),'map_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'script_sha256':script_hash,'builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
