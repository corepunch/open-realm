"""Bound a single owned native worker; prove VM nonreturn separately from math budgets."""
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

PATTERN='stress_wc3_power_lifetime.public_timer_worker'
MARKER=re.compile(r'^POWER350 c=(\d+) P350 tick=(\d+) beat=(\d+) label=(\w+)$')

def run_worker(binary,data,kind,edition,log,hold=12.,startup=30.):
    if kind not in range(4) or edition not in ('classic','tft') or not 0<hold<=60 or not 0<startup<=60:
        raise ValueError('invalid bounded worker request')
    env=dict(os.environ,LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
    env.pop('TEST_SHARD',None);env.pop('TEST_JUNIT',None)
    args=[str(Path(binary).resolve()),'-data',str(Path(data).resolve())]+(['-tft']if edition=='tft'else[])+[
        '+dedicated','1','+set','wc3_power350_case',str(kind),'+test',PATTERN]
    lines=queue.Queue();records=[];before=None;end=None;started=time.monotonic();timed_out=False
    path=Path(log)
    if path.exists():raise ValueError('fresh worker log required')
    proc=subprocess.Popen(args,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
    def drain():
        for line in proc.stdout:lines.put((time.monotonic(),line))
    reader=threading.Thread(target=drain,daemon=True);reader.start()
    try:
        with path.open('w')as out:
            while True:
                try:wall,line=lines.get(timeout=.05)
                except queue.Empty:line=None
                if line is not None:
                    out.write(line);out.flush();m=MARKER.match(line.rstrip())
                    if m:
                        counter,tick,beat=map(int,m.groups()[:3]);label=m.group(4)
                        records.append(dict(counter=counter,tick=tick,beat=beat,label=label))
                        if label=='before':before=wall
                now=time.monotonic();deadline=before+hold if before is not None else started+startup
                if proc.poll()is not None and not reader.is_alive()and lines.empty():break
                if now>=deadline:
                    timed_out=True;end=now;proc.kill();proc.wait(timeout=5);break
        if proc.poll()is None:proc.kill();proc.wait(timeout=5)
        reader.join(timeout=2)
    finally:
        if proc.poll()is None:proc.kill();proc.wait(timeout=5)
        if reader.is_alive():reader.join(timeout=2)
        proc.stdout.close()
    labels=[r['label']for r in records]
    if before is None:raise AssertionError('worker never reached the actual public native; '+str(path))
    if kind==3:
        if timed_out or proc.returncode!=0 or labels.count('after')!=1 or labels.count('complete')!=1:
            raise AssertionError('terminating public control did not complete; '+str(path))
    elif not timed_out or end-before<hold or labels[-1]!='before' or 'after'in labels or 'complete'in labels:
        raise AssertionError('public native returned/recovered instead of retaining the VM; '+str(path))
    before_row=next(r for r in records if r['label']=='before')
    if (before_row['counter'],before_row['tick'],before_row['beat'])!=(1090,20,13):
        raise AssertionError('public scheduler admission differs from retail: '+str(before_row))
    return dict(case=kind,edition=edition,pid=proc.pid,owned=True,timed_out=timed_out,
        hold_seconds=hold,observed_hold_seconds=end-before if timed_out else None,
        exit_code=proc.returncode,records=records)


def main():
    import argparse
    from concurrent.futures import ThreadPoolExecutor
    import json
    import tempfile
    import xml.etree.ElementTree as ET
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--data',type=Path,required=True)
    p.add_argument('--junit',type=Path);p.add_argument('--report',type=Path);a=p.parse_args()
    if a.report and a.report.exists():p.error('fresh report required')
    with tempfile.TemporaryDirectory(prefix='wc3-power-lifetime-')as tmp:
        tmp=Path(tmp)
        def check(pair):
            edition,kind=pair
            return run_worker(a.binary,a.data,kind,edition,tmp/f'{edition}-{kind}.log')
        results=[]
        with ThreadPoolExecutor(max_workers=4)as pool:
            futures={(edition,kind):pool.submit(check,(edition,kind))for edition in ('classic','tft')for kind in range(4)}
            for (edition,kind),future in futures.items():
                try:row=future.result()
                except Exception as error:row=dict(edition=edition,case=kind,error=str(error))
                row['log']=(tmp/f'{edition}-{kind}.log').read_text()if(tmp/f'{edition}-{kind}.log').exists()else ''
                results.append(row)
        failed=sum('error'in row for row in results)
        result=dict(passed=not failed,workers=results)
        if a.report:a.report.write_text(json.dumps(result,indent=2)+'\n')
        if a.junit:
            suite=ET.Element('testsuite',name='wc3-power-lifetime',tests=str(len(results)),failures=str(failed),errors='0',skipped='0')
            for row in results:
                case=ET.SubElement(suite,'testcase',classname='wc3-power-lifetime',name=f"{row['edition']}-{row['case']}")
                if 'error'in row:ET.SubElement(case,'failure',message=row['error'])
                ET.SubElement(case,'system-out').text=row['log']
            ET.ElementTree(suite).write(a.junit,encoding='utf-8',xml_declaration=True)
        for row in results:
            if 'error'in row:print(row['error']+'\n'+row['log'],file=__import__('sys').stderr)
        print(f"{len(results)-failed}/{len(results)} bounded public-native lifetime checks passed")
        if failed:raise SystemExit(1)
if __name__=='__main__':main()
