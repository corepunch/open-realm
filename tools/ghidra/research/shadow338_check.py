#!/usr/bin/env python3
"""Verify bounded original Ashm/Ahid evidence, not engine behavior or factory linkage."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
from invisibility327_availability_check import PE, BASE, ORIGINAL_SHA, require
FIXTURE=Path(__file__).resolve().parents[1]/'fixtures/research/shadow338-original.json'
RANGES={0x50ab40:141,0x50abd0:141,0x50d940:56,0x50d980:56,0x5067b0:142,0x509170:127,0x517b30:114,0x509060:109,0x51e700:29,0x50a960:131,0x50d780:298,0x51d440:241,0x51d1e0:59,0x51cf50:205,0x525be0:153,0x508ae0:69,0x51d0a0:140,0x5ffad0:82}
def check(d,binary):
 require(d['schema']==1 and d['original_sha256']==ORIGINAL_SHA==hashlib.sha256(binary).hexdigest(),'original SHA/schema')
 require(set(d['functions'])=={hex(BASE+r) for r in RANGES},'body inventory')
 p=PE(binary); calls=[]
 for r,n in RANGES.items():
  f=d['functions'][hex(BASE+r)];raw=p.read(r,n)
  require(f['size']==n and int(f['body_start'],16)==BASE+r and int(f['body_end'],16)==BASE+r+n-1,'whole body bounds')
  require(f['original_bytes']==raw.hex() and f['original_sha256']==hashlib.sha256(raw).hexdigest(),'whole body bytes')
  require(bool(f['decompiled_code'].strip()),'decompilation')
  cursor=r
  for i in f['disassembly']:
   a=int(i['address'],16)-BASE;ib=bytes.fromhex(i['bytes'])
   require(a==cursor and 0<len(ib)<=15 and a+len(ib)<=r+n and p.read(a,len(ib))==ib,'instruction coverage')
   cursor+=len(ib)
   if i['mnemonic']=='CALL' and ib[0]==0xe8:
    require(len(ib)==5,'E8 length');target=a+5+struct.unpack('<i',ib[1:])[0]
    require(int(i['operands'],16)==BASE+target,'E8 decoded target');calls.append(dict(rva=a,target_rva=target))
  require(cursor==r+n,'complete disassembly')
 require(len(calls)==96 and sorted(calls,key=lambda c:c['rva'])==d['direct_calls'],'all direct edges')
 require(set(d['vtable_words'])=={'Ashm','Ahid'},'class inventory')
 slots={0xc,0x1c,0xbc,0xc0,0xe0,0xe4,0xe8,0xec,0x12c,0x174,0x178,0x2f4,0x308,0x30c,0x340}
 for cls,tb,get,code in [('Ashm',0xb08110,0x5172a0,0x4173686d),('Ahid',0xb0846c,0x5170f0,0x41686964)]:
  t=d['class_tables'][cls]
  require(t['rva']==tb and t['last_slot']==0x344 and t['last_word']==p.read(tb+0x344,4).hex() and t['following_word']==p.read(tb+0x348,4).hex(),'bounded table (following word is not method)')
  require(t['class_getter_rva']==get and t['class_getter_bytes']==(b'\xb8'+struct.pack('<I',code)+b'\xc3').hex() and p.read(get,6).hex()==t['class_getter_bytes'],'class ID')
  require(set(map(lambda s:int(s,16),d['vtable_words'][cls]))==slots,'15 bounded slots')
  for slot,w in d['vtable_words'][cls].items():
   r=tb+int(slot,16);raw=p.read(r,4)
   require(w['rva']==r and w['bytes']==raw.hex() and w['target_rva']==struct.unpack('<I',raw)[0]-BASE,'original virtual target')
 # Literal d0177 in both activation bodies, and Hold receiver flag200: no fabricated invis call.
 for r in (0x50ab40,0x50abd0):
  require(struct.pack('<I',0xd0177) in p.read(r,RANGES[r]),'hold task event')
 require(any(i['mnemonic']=='OR' and '0x200' in i['operands'] for i in d['functions']['0x6f5ffad0']['disassembly']),'Hold flag')
 return dict(functions=18,shadow_bodies=17,hold_receiver=1,vtable_words=30,direct_calls=96)
def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--binary',required=True,type=Path);a.add_argument('--check',action='store_true');args=a.parse_args()
 d=json.loads(FIXTURE.read_text());b=args.binary.read_bytes();result=check(d,b)
 def missing(x):x['functions'].pop('0x6f50ab40')
 def bounds(x):x['functions']['0x6f509170']['body_end']='6f509171'
 def body(x):x['functions']['0x6f50d940']['original_bytes']='00'
 def trunc(x):x['functions']['0x6f51d440']['disassembly'].pop()
 def edge(x):x['direct_calls'][0]['target_rva']+=1
 def overrun(x):x['class_tables']['Ashm']['last_slot']=0x34c
 def classid(x):x['class_tables']['Ahid']['class_getter_bytes']=x['class_tables']['Ashm']['class_getter_bytes']
 def slot(x):x['vtable_words']['Ahid']['0x12c']['target_rva']=0x51e700
 for mutate in (missing,bounds,body,trunc,edge,overrun,classid,slot):
  bad=copy.deepcopy(d);mutate(bad)
  try:check(bad,b)
  except ValueError:pass
  else:raise ValueError('accepted negative control '+mutate.__name__)
 print(json.dumps(dict(status='PASS',**result,negative_controls=8,scope='original static bodies and slots only')))
if __name__=='__main__':main()
