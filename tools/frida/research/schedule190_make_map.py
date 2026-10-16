#!/usr/bin/env python3
"""Extend the frozen mixed13 Captain map with isolated active separation owners."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
import sep_research_map as sep

BASE_SHA='c84af647961f25512c09f4ecb74aa3d9a283ebf0aa554380162a832dd1c0a60d'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base',type=Path,required=True);ap.add_argument('--tool',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    original=a.base.read_bytes()
    if hashlib.sha256(original).hexdigest()!=BASE_SHA:ap.error('requires frozen mixed13 Captain map')
    if a.output.exists():ap.error('output must be new')
    tool=str(a.tool.resolve());members=[m for m in subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()if not m.endswith('/')and m!='(listfile)']+['Scripts\\wc3_captain_probe.ai']
    sep.UNITS={'hS90':dict(urpo=1,ucol=8.0)}
    changed={}
    with tempfile.TemporaryDirectory()as d:
        root=Path(d);payload=root/'payload.mpq';command=[tool,'-mpq',str(payload),'pack']
        for i,member in enumerate(members):
            data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',member])
            if member=='war3map.w3u':data=sep.w3u_rows(data)
            if member=='war3map.j':
                text=data.decode();anchor='    call PreloadGenStart()';assert text.count(anchor)==1
                text=text.replace(anchor,anchor+'\n    call CreateUnit(Player(0),\'hS90\',4000.0,4000.0,0.0)\n    call CreateUnit(Player(0),\'hS90\',4400.0,4000.0,0.0)',1)
                text=text.replace('pathtrace-captain_home.txt','rs-schedule190.txt');data=text.encode()
            if member in ('war3map.j','war3map.w3u'):
                changed[member]=hashlib.sha256(data).hexdigest();a.output.with_suffix('.'+member.split('.')[-1]).write_bytes(data)
            p=root/str(i);p.write_bytes(data);command.extend([str(p),member])
        subprocess.run(command,check=True);a.output.write_bytes(original[:512]+payload.read_bytes())
    meta=dict(base_sha256=BASE_SHA,map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),changed_members=changed,
              task='SCHED-02.3',isolated_repulsors=2,preload='rs-schedule190.txt')
    a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))


if __name__=='__main__':main()
