#!/usr/bin/env python3
"""Prepare common Morph availability settings controls; no retail behavior assertion."""
import argparse,hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent

def sha(raw):return hashlib.sha256(raw).hexdigest()
def ability_objects():
    codes=[b'Abrf',b'Arav',b'Apiv'];out=struct.pack('<II',2,len(codes))
    for code in codes:
        mods=[(b'aher',0,0,struct.pack('<i',0)),(b'areq',3,0,b'\0'),
              (b'amcs',0,1,struct.pack('<i',0)),(b'acdn',2,1,struct.pack('<f',0)),
              (b'adur',2,1,struct.pack('<f',0.625 if code==b'Apiv'else 0.375)),
              (b'ahdu',2,1,struct.pack('<f',0))]
        out+=code+b'\0'*4+struct.pack('<I',len(mods))
        for field,kind,level,value in mods:
            out+=field+struct.pack('<III',kind,level,0)+value+b'\0'*4
    return out+struct.pack('<I',0)

def clear_original_ability_lists(data):
    """Retain the frozen four movement-policy rows and add only their uabi override."""
    if struct.unpack_from('<I',data)[0]not in(1,2):raise ValueError('unsupported unit object version')
    cursor=4;out=data[:4];found=set();targets={b'edoc',b'edcm',b'edot',b'edtm'}
    for section in range(2):
        count=struct.unpack_from('<I',data,cursor)[0];out+=data[cursor:cursor+4];cursor+=4
        for _ in range(count):
            start=cursor;old,new,fields=struct.unpack_from('<4s4sI',data,cursor);cursor+=12;names=[]
            for _ in range(fields):
                field,kind=struct.unpack_from('<4sI',data,cursor);names.append(field);cursor+=8
                if kind==3:cursor=data.index(b'\0',cursor)+1
                elif kind in(0,1,2):cursor+=4
                else:raise ValueError('unsupported unit object field')
                cursor+=4
            if section==0 and old in targets and new==bytes(4):
                if old in found or b'uabi'in names:raise ValueError('unexpected existing unit ability override')
                found.add(old);out+=data[start:start+8]+struct.pack('<I',fields+1)+data[start+12:cursor]
                out+=b'uabi'+struct.pack('<I',3)+bytes(5)
            else:out+=data[start:cursor]
    if cursor!=len(data)or found!=targets:raise ValueError('frozen unit row inventory differs')
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--mpqtool',type=Path,required=True)
    p.add_argument('--alternate-disable',choices=['default','0','1'],default='default')
    a=p.parse_args()
    if a.output.exists():p.error('fresh output required')
    source=(HERE/'morph374_probe.j').read_bytes()
    probe=source.replace(b'integer udg_M374Setting=-1',('integer udg_M374Setting='+('-1'if a.alternate_disable=='default'else a.alternate_disable)).encode())
    with tempfile.TemporaryDirectory(prefix='morph374-')as tmp:
        tmp=Path(tmp);probe_path=tmp/'probe.j';probe_path.write_bytes(probe);intermediate=tmp/'input.w3m'
        subprocess.run([sys.executable,str(HERE/'morph330_make_map.py'),'--base',str(a.base),
            '--output',str(intermediate),'--mpqtool',str(a.mpqtool),'--probe',str(probe_path)],check=True,stdout=subprocess.DEVNULL)
        def cat(name):return subprocess.check_output([str(a.mpqtool),'-mpq',str(intermediate),'cat',name])
        names=[n for n in cat('(listfile)').decode().splitlines()if n!='(listfile)']
        if 'war3mapMisc.txt'in names:raise ValueError('base unexpectedly authors Misc; inspect before replacing')
        if a.alternate_disable!='default':names.append('war3mapMisc.txt')
        packed=tmp/'payload.mpq';command=[str(a.mpqtool),'-mpq',str(packed),'pack'];members={}
        for i,name in enumerate(names):
            if name=='war3mapMisc.txt':data=('[Misc]\nMorphAlternateDisable='+a.alternate_disable+'\n').encode()
            elif name=='war3map.w3a':data=ability_objects()
            elif name=='war3map.w3u':data=clear_original_ability_lists(cat(name))
            else:data=cat(name)
            path=tmp/str(i);path.write_bytes(data);command.extend([str(path),name]);members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(intermediate.read_bytes()[:512]+packed.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(task='SEP-01.2',phase='common-morph374-prepared-unobserved',
        alternate_disable=a.alternate_disable,base_sha256=sha(a.base.read_bytes()),map_sha256=sha(a.output.read_bytes()),
        members=members,probe_sha256=sha(probe),probe_source_sha256=sha(source),builder_sha256=sha(Path(__file__).read_bytes()),
        parent_builder_sha256=sha((HERE/'morph330_make_map.py').read_bytes())),indent=2)+'\n')
if __name__=='__main__':main()
