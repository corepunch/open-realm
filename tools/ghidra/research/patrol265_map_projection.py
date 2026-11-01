#!/usr/bin/env python3
"""Literal265 map geometry; deliberately separate from zero-WPM lifetime124b."""
import argparse,collections,hashlib,json,pathlib,shutil,struct,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_patrol265_map.json'
def extract(map_path,tool):
 def member(n):return subprocess.check_output([str(tool),'-mpq',str(map_path),'cat',n])
 wpm=member('war3map.wpm');w3e=member('war3map.w3e');w3i=member('war3map.w3i')
 width,height=struct.unpack_from('<II',wpm,8);assert len(wpm)==16+width*height
 o=13;ground=struct.unpack_from('<I',w3e,o)[0];o+=4+4*ground;cliff=struct.unpack_from('<I',w3e,o)[0];o+=4+4*cliff
 vw,vh,ox,oy=struct.unpack_from('<IIff',w3e,o);o+=16;assert len(w3e)==o+vw*vh*7
 vertices=[list(struct.unpack_from('<HHBBB',w3e,i)) for i in range(o,len(w3e),7)]
 io=12
 for _ in range(4):io=w3i.index(0,io)+1
 camera=list(struct.unpack_from('<8f',w3i,io));complements=list(struct.unpack_from('<4i',w3i,io+32));iw,ih,flags=struct.unpack_from('<III',w3i,io+48)
 return {'schema':1,'map_sha256':hashlib.sha256(pathlib.Path(map_path).read_bytes()).hexdigest(),'members':{n:{'sha256':hashlib.sha256(b).hexdigest(),'hex':b.hex()} for n,b in [('war3map.wpm',wpm),('war3map.w3e',w3e),('war3map.w3i',w3i)]},'wpm':{'width':width,'height':height,'cells':list(wpm[16:])},'terrain':{'vertices_width':vw,'vertices_height':vh,'origin':[ox,oy],'vertices':vertices},'w3i':{'version':struct.unpack_from('<I',w3i)[0],'camera':camera,'complements':complements,'declared_size':[iw,ih],'flags':flags},'limitations':['W3I camera bounds and declared size are inheritedbase metadata, not W3E physicalworld dimensions.','Literal265 WPM includes fullheight columns20..23 wall; initial flat Cfixture was incorrect.','Lifetime124b is a different zero-WPM map.']}
def header(d):
 s='#ifndef RETAIL_PATROL265_MAP_H\n#define RETAIL_PATROL265_MAP_H\n/* Generated from actual frozen265 MPQ members; no inferred terrain. */\n'
 s+='static unsigned char const patrol_native_wpm265[4096] = {\n'
 a=d['wpm']['cells'];assert len(a)==4096
 for i in range(0,len(a),32):s+='    '+','.join('0x%02x'%v for v in a[i:i+32])+',\n'
 s+='};\nstatic unsigned short const patrol_native_terrain265[289][5] = {\n'
 for v in d['terrain']['vertices']:s+='    {'+','.join(map(str,v))+'},\n'
 return s+'};\n#endif\n'
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--map',required=True);ap.add_argument('--mpqtool');ap.add_argument('--check',action='store_true');a=ap.parse_args()
 tool=a.mpqtool or shutil.which('mpqtool') or ROOT/'build/bin/mpqtool';d=extract(a.map,tool);assert d==json.loads(FIXTURE.read_text()),'literal map projection differs'
 assert FIXTURE.with_suffix('.h').read_text()==header(d),'semantic map header differs'
 p=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
 for n,h in p['repository_files'].items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
 for key in ('wpm','terrain'):
  q=json.loads(json.dumps(d));q[key]['cells' if key=='wpm' else 'vertices'][0]=0;assert q!=d
 print(json.dumps({'status':'PASS','wpm_cells':len(d['wpm']['cells']),'terrain_vertices':len(d['terrain']['vertices']),'header_checked':True}))
if __name__=='__main__':main()
