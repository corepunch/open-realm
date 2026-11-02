#!/usr/bin/env python3
"""Preserve the accepted LT06 terrain/deck fixture, replace its probe and UnitUI overrides."""
import argparse,hashlib,json,re,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BASE_SHA='5ef45d6b906c270734b26931772194ea3306676f68714e078c12eb1841785e06'
def build_script(base):
 probe=(HERE/'pose272_probe.j').read_text()
 for old,new in [('call CreateAllUnits(  )','call PathProbeInit()'),('call InitCustomTriggers(  )','// Pose probe owns triggers.'),('call RunInitializationTriggers(  )','// Pose probe owns initialization.'),('call CreateRegions(  )','// No campaign regions.'),('call CreateCameras(  )','// No campaign cameras.')]:
  if base.count(old)!=1:raise ValueError('unexpected base script: '+old)
  base=base.replace(old,new)
 base=re.sub(r'call SetCameraBounds\( [^\n]+','call SetCameraBounds( 128, 128, 1920, 1920, 128, 1920, 1920, 128 )',base)
 block=re.search(r'^globals\n(.*?)^endglobals\n',probe,re.M|re.S)
 if block is None or base.count('\nendglobals')!=1:raise ValueError('globals differ')
 base=base.replace('\nendglobals','\n'+block.group(1)+'endglobals',1)
 return base.replace('\nendglobals','\nendglobals\n'+probe[block.end():],1)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--script-base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 if hashlib.sha256(a.base.read_bytes()).hexdigest()!=BASE_SHA:p.error('accepted fixture differs')
 tool=str(ROOT/'build/bin/mpqtool');members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();changed={}
 with tempfile.TemporaryDirectory(prefix='pose272-')as tmp:
  temp=Path(tmp);payload=temp/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,name in enumerate(members):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name=='war3map.j':
    original=subprocess.check_output([tool,'-mpq',str(a.script_base),'cat',name]).decode().replace('\r\n','\n');data=build_script(original).encode()
   elif name=='war3map.w3u':
    # Preserve original rows and append clones to the second table.
    cursor=4
    if struct.unpack_from('<I',data)[0] not in (1,2):raise ValueError('unexpected object version')
    for table in range(2):
     count_at=cursor;count=struct.unpack_from('<I',data,cursor)[0];cursor+=4
     for _ in range(count):
      n=struct.unpack_from('<I',data,cursor+8)[0];cursor+=12
      for _ in range(n):
       kind=struct.unpack_from('<I',data,cursor+4)[0];cursor+=8
       cursor=data.index(b'\0',cursor)+1 if kind==3 else cursor+4
       cursor+=4
    if cursor!=len(data):raise ValueError('trailing object bytes')
    data=data[:count_at]+struct.pack('<I',count+2)+data[count_at+4:]
    for code,pitch,roll in [(b'hP72',3.,5.),(b'hZ72',0.,0.)]:
     mods=[(b'umxp',pitch),(b'umxr',roll),(b'uerd',20.)]
     data+=b'hfoo'+code+struct.pack('<I',len(mods))
     for field,value in mods:data+=field+struct.pack('<If',1,value)+code
   elif name=='war3map.shd':data=bytes(64*64)
   changed[name]=hashlib.sha256(data).hexdigest();path=temp/str(i);path.write_bytes(data);cmd += [str(path),name]
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 meta=dict(task='MOVE-03.3',base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),script_base_sha256=hashlib.sha256(a.script_base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),members=changed,probe_sha256=hashlib.sha256((HERE/'pose272_probe.j').read_bytes()).hexdigest(),builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
 a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(meta['map_sha256'])
if __name__=='__main__':main()
