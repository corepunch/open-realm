#!/usr/bin/env python3
"""Render literal read-only retail crossing expectations; no expected arithmetic."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_slope272.h'
def render(spec):
 def v(a):return '{'+','.join(v(x) if isinstance(x,list) else hex(x)+'u' for x in a)+'}'
 out=['#ifndef RETAIL_SLOPE272_H','#define RETAIL_SLOPE272_H','/* MOVE-03.3: original published support and orientation, Pose272. */',
 'static uint32_t const slope272_grid[]='+v(spec['grid']['values'])+';',
 'static uint8_t const slope272_pathing[]='+v(spec['pathing'])+';',
 'static struct {uint32_t type,point[3],heading,flags,limits[2],angles[3],matrix[9];} const slope272_cases[]={']
 for c in spec['cases']:out.append('{'+','.join(v(c[k]) if isinstance(c[k],list) else hex(c[k])+'u' for k in ('type','point','heading','flags','limits','angles','matrix'))+'},')
 out.append('};');out.append('static struct {uint32_t point[2],heights[4],result;} const slope272_ground[]={');
 for c in spec['original']['ground_kernels']:out.append('{'+v(c['point'])+','+v(c['heights'])+','+hex(c['result'])+'u},')
 out.extend(['};','#endif']);return '\n'.join(out)+'\n'
