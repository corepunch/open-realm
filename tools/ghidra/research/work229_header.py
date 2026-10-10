#!/usr/bin/env python3
"""Render literal original-code and live walkable mesh inputs/expectations."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_walkmesh229.h'
def render(spec):
 out=['/* Original game.dll instructions and read-only MAP-02.2 captures; Work229. */']
 def values(a):return '{'+','.join(values(x)if isinstance(x,list)else hex(x)+'u'for x in a)+'}'
 out+=['static struct {uint32_t input[5][3],hit,distance;} const walkmesh229_triangles[]={']
 for c in spec['kernels']['cases']:out.append('{'+values(c['input'])+','+str(c['hit'])+','+hex(c['distance'])+'u},')
 out+=['};','static struct {uint32_t input[3],matrix[12],output[3];} const walkmesh229_transforms[]={']
 for c in spec['kernels']['transforms']:out.append('{'+','.join(values(c[k])for k in ['input','matrix','output'])+'},')
 out+=['};','static struct {uint32_t input[3][3],distance,fraction;} const walkmesh229_distances[]={']
 for c in spec['kernels']['distances']:out.append('{'+values(c['input'])+','+hex(c['distance'])+'u,'+hex(c['fraction'])+'u},')
 out+=['};']
 for key,kind in [('vertices','uint32_t'),('indices','uint16_t'),('matrices','uint32_t'),('transformed','uint32_t')]:out.append('static '+kind+' const walkmesh229_'+key+'[]='+values(spec['mesh'][key])+';')
 out+=['static struct {uint32_t point[2],hit,height;} const walkmesh229_points[]={']
 for c in spec['queries']:out.append('{'+values(c['point'])+','+str(c['hit'])+','+hex(c['height'])+'u},')
 out+=['};']
 out+=['static uint8_t const walkmesh229_terrain[]='+values(spec['terrain'])+';']
 out+=['static struct {char const *type;float x,y;uint32_t height,deck,deep;} const walkmesh229_support[]={']
 for c in spec['support_cases']:out.append('{"'+c['type']+'",'+','.join(str(x) for x in c['point'])+','+hex(c['height'])+'u,'+str(c['deck'])+','+str(c['deep'])+'},')
 out+=['};']
 return '\n'.join(out)+'\n'
if __name__=='__main__':HEADER.write_text(render(json.loads((ROOT/'tools/ghidra/fixtures/retail-work229-1.27.json').read_text())))
