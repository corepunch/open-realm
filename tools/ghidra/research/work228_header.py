#!/usr/bin/env python3
"""Render literal original-code and live retail flight-support expectations."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_flight_support228.h'
def render(spec):
 out=['/* Original game.dll instructions and read-only MAP-02.2 captures; Work228. */',
      '#ifndef BZ_RETAIL_FLIGHT_SUPPORT228_H', '#define BZ_RETAIL_FLIGHT_SUPPORT228_H']
 def array(name,a):out.append('static uint32_t const '+name+'[] = {'+','.join(hex(x)+'u' for x in a)+'};')
 for i,c in enumerate(spec['kernels']['maximum']):array('flight228_in'+str(i),c['input']);array('flight228_out'+str(i),c['output'])
 out.append('static struct { uint32_t width,height,radius; uint32_t const *input,*output; } const flight228_max[] = {')
 for i,c in enumerate(spec['kernels']['maximum']):out.append('{'+','.join(str(c[k])for k in ['width','height','radius'])+',flight228_in'+str(i)+',flight228_out'+str(i)+'},')
 out.append('};');g=spec['kernels']['sample_grid'];array('flight228_grid',g['bits'])
 out.append('static struct { uint32_t x,y,z; } const flight228_samples[] = {')
 for c in spec['kernels']['samples']:out.append('{'+','.join(hex(x)+'u' for x in [*c['point'],c['result']])+'},')
 out.append('};');array('flight228_map',spec['completed_grid']['bits'])
 return '\n'.join(out)+'\n#endif\n'
if __name__=='__main__':HEADER.write_text(render(json.loads((ROOT/'tools/ghidra/fixtures/retail-work228-1.27.json').read_text())))
