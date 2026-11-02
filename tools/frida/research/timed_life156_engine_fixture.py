#!/usr/bin/env python3
"""Build the engine replay from frozen retail markers and the public producer."""
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]

def render():
    markers=json.loads((ROOT/'tools/ghidra/fixtures/retail-timed-life-live-1.27.json').read_text())['public_markers']
    script=(ROOT/'tools/frida/research/timed_life156_probe.j').read_text()
    helpers="""function ModuloInteger takes integer a, integer b returns integer
return a-(a/b)*b
endfunction
function IntegerTertiaryOp takes boolean b, integer a, integer c returns integer
if b then
return a
endif
return c
endfunction
"""
    script=script.replace('endglobals\n','endglobals\n'+helpers,1)
    script=('native SetUnitUseFood takes unit u, boolean flag returns nothing\n'
            'native CreateTimer takes nothing returns timer\n'
            'native TimerStart takes timer t, real timeout, boolean periodic, code handler returns nothing\n'+script+
            'function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n')
    return ('/* Frozen public markers: two observed retail repeats and observer-free control. */\n'
            'static char const *const timedlife_markers_156[]={\n'+
            ''.join('    '+json.dumps(m)+',\n' for m in markers)+
            '};\nstatic char const timedlife_script_156[]=\n'+
            ''.join('    '+json.dumps(line)+'\n' for line in script.splitlines(True))+';\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    path=ROOT/'games/warcraft-3/game/tests/retail_timed_life_156.h';expected=render()
    if args.check:
        if path.read_text()!=expected:raise SystemExit('engine replay differs from frozen producer')
    else:path.write_text(expected)
if __name__=='__main__':main()
