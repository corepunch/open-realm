#!/usr/bin/env python3
"""Build authored seed/race startup maps; no installed Warcraft assets needed."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from make_pathing_reload_maps import members


def script(variant):
    prefs = ([1]*4 + [2,32,8,32,4,32,1,32] if variant=='mixed'
             else [1]*12 if variant=='fixed' else [1]*4)
    config = ''.join('call SetPlayerRacePreference(Player(%d),ConvertRacePref(%d))\n' % (i,p)
                     for i,p in enumerate(prefs))
    queries = '' if variant=='triad' else ('set seedInt=GetRandomInt(0,2147483647)\n'
        'set seedInt2=GetRandomInt(-1000,1000)\nset seedReal=GetRandomReal(0,1)\n')
    return ('globals\ninteger seedInt=0\ninteger seedInt2=0\nreal seedReal=0\nunit seedMover=null\nendglobals\n'
        'function config takes nothing returns nothing\n'+config+'endfunction\n'
        'function main takes nothing returns nothing\n'+queries+
        "set seedMover=CreateUnit(Player(0),'hfoo',272,304,90)\n"
        'call SetUnitMoveSpeed(seedMover,80)\ncall IssuePointOrder(seedMover,"move",1552,1776)\n'
        'endfunction\n').encode()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mpqtool',type=Path,default=Path('build/bin/mpqtool'))
    ap.add_argument('--output',type=Path,default=Path('games/warcraft-3/tests/resources-src/Maps/Test'))
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for variant in ('mixed','fixed','triad'):
            command=[str(args.mpqtool),'-mpq',str(args.output/('PathingSeed'+variant.title()+'.w3m')),'pack']
            data=members(17);data['war3map.j']=script(variant)
            for member,raw in data.items():
                path=root/member;path.write_bytes(raw);command.extend([str(path),member])
            subprocess.run(command,check=True)


if __name__=='__main__':
    main()
