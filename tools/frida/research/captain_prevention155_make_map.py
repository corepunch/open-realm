#!/usr/bin/env python3
"""Public buff refresh, expiry and Attack remove/re-add probe; no game-memory writes."""
import argparse,hashlib,importlib.util,json,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('captain_range_builder',HERE/'GROUP-03.4.6.2.1.2_make_map.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
def abilities():
 entries=[(b'APM1',1,600.),(b'APM2',2,600.),(b'APMS',1,.5),(b'APM8',8,600.)]
 out=struct.pack('<III',2,0,len(entries))
 for code,mask,duration in entries:
  mods=[(b'aher',0,0,0,0),(b'Nsi1',0,1,1,mask),(b'Nsi2',2,1,2,0.),(b'Nsi3',2,1,3,0.),(b'Nsi4',2,1,4,0.),(b'atar',3,1,0,base.TARGETS),(b'aran',2,1,0,9999.),(b'amcs',0,1,0,0),(b'acdn',2,1,0,0.),(b'adur',2,1,0,duration),(b'ahdu',2,1,0,duration)]
  out+=b'ANdh'+code+struct.pack('<I',len(mods))
  for field,kind,rank,column,value in mods:out+=field+struct.pack('<III',kind,rank,column)+base.value(kind,value)+code
 return out
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 original=a.base.read_bytes();assert original[:4]==b'HM3W' and original[512:516]==b'MPQ\x1a'
 members=subprocess.check_output([str(a.tool),'-mpq',str(a.base),'ls'],text=True).splitlines();changed={}
 probe=(HERE/'captain_prevention155_probe.j').read_text()
 with tempfile.TemporaryDirectory() as temp:
  root=Path(temp);payload=root/'payload.mpq';command=[str(a.tool),'-mpq',str(payload),'pack']
  for i,name in enumerate(members):
   data=subprocess.check_output([str(a.tool),'-mpq',str(a.base),'cat',name])
   if name=='war3map.j':data=base.mwpm.instrument(data.decode().replace('\r\n','\n'),probe,'captain_home').encode()
   if name=='war3map.w3u':data=base.append_units(data)
   source=root/str(i);source.write_bytes(data);command+=[str(source),name]
   if name in ('war3map.j','war3map.w3u'):changed[name]=hashlib.sha256(data).hexdigest();a.output.with_suffix('.'+name.split('.')[-1]).write_bytes(data)
  source=root/'abilities';source.write_bytes(abilities());command+=[str(source),'war3map.w3a'];subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
  a.output.write_bytes(original[:512]+payload.read_bytes());a.output.with_suffix('.w3a').write_bytes(source.read_bytes())
 result=dict(binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',base_sha256=hashlib.sha256(original).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),probe_sha256=hashlib.sha256(probe.encode()).hexdigest(),changed_members=changed)
 a.output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
