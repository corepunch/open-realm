#!/usr/bin/env python3
"""Build the small, authored MPQs used by wc3_map_lifetime (no retail assets)."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


SCRIPT = b'''function config takes nothing returns nothing
call SetMapFlag(ConvertMapFlag(32768),true)
endfunction
function main takes nothing returns nothing
local unit u=CreateUnit(Player(0),'hfoo',272,304,90)
call SetUnitMoveSpeed(u,80)
call IssuePointOrder(u,"move",1552,1776)
endfunction
'''


def members(width):
    # ROC W3I18, one human player; flat native terrain and walkable WPM.
    info = struct.pack('<III',18,0,6059)+b'Pathing reload\0OpenRealm\0\0One player\0'
    info += struct.pack('<8f4i2IIBI',128,128,128,1920,1920,1920,1920,128,
                        0,0,0,0,width-1,16,0,ord('L'),0xffffffff)
    info += b'\0\0\0'+struct.pack('<I',0)+b'\0\0\0'
    info += struct.pack('<5I',1,0,1,1,0)+b'Player\0'+struct.pack('<2f3I',272,304,0,0,0)
    terrain = b'W3E!'+struct.pack('<IBIII',11,ord('L'),0,0,0)
    terrain += struct.pack('<II2f',width,17,0,0)
    terrain += struct.pack('<HHBBB',0x2000,0x2000,0,0,2)*(width*17)
    pathing = b'MP3W'+struct.pack('<III',0,(width-1)*4,64)+bytes((width-1)*4*64)
    return {'war3map.w3i':info,'war3map.w3e':terrain,'war3map.wpm':pathing,'war3map.j':SCRIPT}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mpqtool',type=Path,default=Path('build/bin/mpqtool'))
    parser.add_argument('--output',type=Path,default=Path('games/warcraft-3/tests/resources-src/Maps/Test'))
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for name,width in [('PathingReload.w3m',17),('PathingChangeLevel.w3m',25)]:
            command=[str(args.mpqtool),'-mpq',str(args.output/name),'pack']
            for member,data in members(width).items():
                path=root/member
                path.write_bytes(data)
                command.extend([str(path),member])
            subprocess.run(command,check=True)


if __name__=='__main__':
    main()
