#!/usr/bin/env python3
"""Freeze original strategic-region inputs and complete outputs; no engine execution."""
import argparse,gzip,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_captain358 import public

def load(path):
 rows=[json.loads(line) for line in path.read_text().splitlines()]
 built=next(r for r in rows if r.get('event')=='region-built')
 filtered=next(r for r in rows if r.get('event')=='region-filtered')
 return dict(width=built['inputWords'][0],height=built['inputWords'][1],cells=built['cells'],scale=built['state']['scale'],sources=[{k:r[k]for k in ('type','words','counter')}for r in rows if r.get('event')=='region-source-position'],built=built['state'],filtered={k:filtered[k]for k in ('selected','state','counter')},point_before=next(r for r in rows if r.get('event')=='region-consumer-begin')['point'],draws=[{k:r[k]for k in ('before','after','result','counter')}for r in rows if r.get('event')=='purpose13-state'],point_after=next(r for r in rows if r.get('event')=='region-random-point')['point'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,action='append',required=True);p.add_argument('--control',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('fresh output required')
 assert len(a.capture)>=2
 publics=[public(x.with_name(x.stem+'-preload.txt').read_text())for x in a.capture+[a.control]]
 assert len(publics[0])==331 and all(x==publics[0]for x in publics[1:]),'public observation/control markers differ'
 cases=[load(x)for x in a.capture];assert all(x==cases[0]for x in cases[1:]),'independent native inputs/outputs differ'
 d=dict(schema=1,scope='Original retail strategic-region kernel input/output; not a full engine journey assertion',binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',origin=[-7168,-3072],public_markers=publics[0],captures=[dict(path=x.name,sha256=hashlib.sha256(x.read_bytes()).hexdigest())for x in a.capture],control=dict(path=a.control.name,sha256=hashlib.sha256(a.control.read_bytes()).hexdigest()),case=cases[0])
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(gzip.compress((json.dumps(d,separators=(',',':'))+'\n').encode(),mtime=0));print(json.dumps(dict(output=str(a.output),sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),nodes=d['case']['built']['count'],grid_bytes=len(d['case']['built']['grid']))))
if __name__=='__main__':main()
