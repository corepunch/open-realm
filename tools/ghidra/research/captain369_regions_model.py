#!/usr/bin/env python3
"""Research model of original Town strategic graph; not an engine parity gate."""
import argparse,json,struct,sys
from pathlib import Path
sys.setrecursionlimit(10000)
FREE={ord('.'),ord('+'),ord('X')}
NEIGHBORS=[(-1,1),(0,1),(1,1),(-1,0),(1,0),(-1,-1),(0,-1),(1,-1)]
CARDINAL=[(1,0),(0,1),(0,-1),(-1,0)]
DIAGONAL=[(1,1),(1,-1),(-1,1),(-1,-1)]

def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def scalar(word):return struct.unpack('<f',struct.pack('<I',word))[0]

class Graph:
 def __init__(self,width,height,classes,tables):
  self.width=width;self.height=height;self.grid=bytearray(65536);self.nodes=[]
  self.patterns=[bytes.fromhex(tables[x])for x in('6fcfcca8','6fcfcba0','6fcfcdb0','6fcfca98')]
  self.counts=struct.unpack('<256I',bytes.fromhex(tables['6fcfceb8']))
  for y in range(height):
   for x in range(width):self.grid[y*256+x]=ord('.')if classes[y*width+x]==0 else ord('#')if classes[y*width+x]==1 else ord('?')
 def mask(self,grid,x,y):
  return sum((1<<i)for i,(dx,dy)in enumerate(NEIGHBORS)if not(0<=x+dx<self.width and 0<=y+dy<self.height)or grid[(y+dy)*256+x+dx]not in FREE)
 def construct(self):
  changed=True
  while changed:
   changed=False
   for table in self.patterns:
    snapshot=self.grid[:]
    for y in range(self.height):
     for x in range(self.width):
      i=y*256+x
      if snapshot[i]==ord('.')and table[self.mask(snapshot,x,y)]==ord('@'):self.grid[i]=ord('@');changed=True
  #9b79f0 performs this pass in-place, including non-dot centre cells.
  for y in range(self.height):
   for x in range(self.width):
    if not(self.mask(self.grid,x,y)&0x5a):self.grid[y*256+x]=ord('@')
  snapshot=self.grid[:]
  for y in range(self.height):
   for x in range(self.width):
    i=y*256+x
    if snapshot[i]==ord('.')and self.counts[self.mask(snapshot,x,y)]>2:self.grid[i]=ord('X')
  for y in range(self.height):
   for x in range(self.width):
    if self.grid[y*256+x]==ord('X'):self.nodes.append([x,y,0,0,0,0,4,-1,-1,-1,-1,4,-1,-1,-1,-1])
  self.lookup={(n[0],n[1]):i for i,n in enumerate(self.nodes)}
  for i,n in enumerate(self.nodes):
   if not n[2]:self.trace(i)
  for i,n in enumerate(self.nodes):
   k=i%62;self.grid[n[1]*256+n[0]]=k+65 if k<26 else k+71 if k<52 else k-4
  return self
 def edge(self,source,target,cost):
  node=self.nodes[source]
  if target in node[7:11]:
   slot=node[7:11].index(target);node[12+slot]=min(node[12+slot],cost);return
  if -1 in node[7:11]:
   slot=node[7:11].index(-1);node[7+slot]=target;node[12+slot]=cost
 def walk(self,source,steps,cost,x,y):
  steps+=1
  if not(0<=x<self.width and 0<=y<self.height):return False
  i=y*256+x
  if self.grid[i]==ord('.'):
   self.grid[i]=ord('+')
   for dx,dy in CARDINAL:
    if self.walk(source,steps,cost+169,x+dx,y+dy):return True
   for dx,dy in DIAGONAL:
    if self.walk(source,steps,cost+239,x+dx,y+dy):return True
  elif self.grid[i]==ord('X'):
   target=self.lookup.get((x,y),0)
   if target!=source:
    self.edge(source,target,cost)
    if steps>=2:self.edge(target,source,cost)
    return True
  return False
 def trace(self,source):
  n=self.nodes[source];x,y=n[:2];n[2]=1
  down=self.walk(source,0,169,x,y+1);up=self.walk(source,0,169,x,y-1)
  left=self.walk(source,0,169,x-1,y);right=self.walk(source,0,169,x+1,y)
  if not down:
   if not right:self.walk(source,0,239,x+1,y+1)
   if not left:self.walk(source,0,239,x-1,y+1)
  if not up:
   if not right:self.walk(source,0,239,x+1,y-1)
   if not left:self.walk(source,0,239,x-1,y-1)
 def nearest(self,point,scale=4,origin=(-7168,-3072)):
  best=-1;distance=409600.
  for i,n in enumerate(self.nodes):
   dx=f32(f32(n[0]*scale*32+origin[0])-point[0]);dy=f32(f32(n[1]*scale*32+origin[1])-point[1])
   squared=f32(f32(dx*dx)+f32(dy*dy))
   if squared<=distance:best=i;distance=squared
  return best
 def expand(self):
  # Exact resulting marks, with shorter-path pruning of the pure native recursion.
  best=[2501]*len(self.nodes)
  def visit(i,d):
   if d>=2501 or d>=best[i]:return
   best[i]=d;self.nodes[i][4]=1
   for j,cost in zip(self.nodes[i][7:11],self.nodes[i][12:16]):
    if j!=-1:visit(j,d+cost)
  for i,n in enumerate(self.nodes):
   if n[3]:visit(i,0)
 def eligible(self,target):
  result=[]
  def visit(i):
   if i==target:return True
   if i==-1 or self.nodes[i][5]or self.nodes[i][4]:return False
   self.nodes[i][5]=1
   return any(visit(j)for j in self.nodes[i][7:11])
  for i,n in enumerate(self.nodes):
   if not n[4]:continue
   for row in self.nodes:row[5]=0
   if any(visit(j)for j in n[7:11]):result.append(i)
  return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,required=True);p.add_argument('--tables',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 rows=[json.loads(l)for l in a.capture.read_text().splitlines()];tables=dict(l.split('|')for l in a.tables.read_text().splitlines())
 built=next(r for r in rows if r.get('event')=='region-built');expected=built['state'];width,height=built['inputWords'][:2]
 g=Graph(width,height,[v>>30 for v in built['cells'][1::2]],tables).construct()
 for r in rows:
  if r.get('event')=='region-source-position'and r.get('type')!='captain':
   node=g.nearest(list(map(scalar,r['words'])));
   if node!=-1:g.nodes[node][3]=g.nodes[node][4]=1
 g.expand();actual=[v&0xffffffff for node in g.nodes for v in node]
 node_differences=[(i,x,y)for i,(x,y)in enumerate(zip(actual,expected['regions']))if x!=y]
 grid_differences=[(i,x,y)for i,(x,y)in enumerate(zip(g.grid,expected['grid']))if x!=y]
 actor=next(r for r in rows if r.get('event')=='region-source-position'and r.get('type')=='captain');nearest=g.nearest(list(map(scalar,actor['words'])));eligible=g.eligible(nearest)
 report=dict(status='analysis-only',nodes=len(g.nodes),expected_nodes=expected['count'],node_differences=node_differences,grid_differences=grid_differences,nearest=nearest,eligible=eligible)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print({k:v if not isinstance(v,list)else len(v)for k,v in report.items()})
if __name__=='__main__':main()
