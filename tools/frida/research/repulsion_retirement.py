#!/usr/bin/env python3
"""Measure real Move retirement work in the headless regression with Frida.

Build the test module with BZ_TESTS, then supply its path. LD_LIBRARY_PATH
keeps any caller-provided runtime dependencies after the selected game module.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import threading
import frida


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--runner',type=Path,default=Path('build/bin/openwarcraft3-tests'))
    parser.add_argument('--data',type=Path,default=Path('build/tests'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();library=args.library.resolve()
    symbols={r.split()[-1]:int(r.split()[0],16) for r in subprocess.check_output(
        ['nm','--defined-only',str(library)],text=True).splitlines() if len(r.split())==3}
    entry=symbols['wc3_repulsion_overlap_ascending_retirement_visits_a_linear_number_of_owners_fn']
    counter=symbols['move_test_repulse_unlink_visits']
    env=dict(os.environ);env['LD_LIBRARY_PATH']=str(library.parent)+':'+env.get('LD_LIBRARY_PATH','')
    device=frida.get_local_device();pid=device.spawn([str(args.runner),'-data',str(args.data),
        '+dedicated','1','+test','wc3_repulsion_overlap.ascending_retirement_visits_a_linear_number_of_owners'],env=env,stdio='pipe')
    rows=[];done=threading.Event()
    try:
        session=device.attach(pid);session.on('detached',lambda *unused:done.set())
        code=f"const m=Process.getModuleByName({json.dumps(library.name)});Interceptor.attach(m.base.add({entry}),{{onLeave(){{send({{owners:1024,visits:m.base.add({counter}).readU64().toString()}});}}}});"
        script=session.create_script(code);script.on('message',lambda message,data:rows.append(message));script.load();device.resume(pid)
        if not done.wait(45):raise RuntimeError('retirement test did not complete')
        if len(rows)!=1 or rows[0]['type']!='send':raise RuntimeError('invalid work report: '+repr(rows))
        result=rows[0]['payload'];args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    finally:
        if not done.is_set():device.kill(pid)


if __name__=='__main__':main()
