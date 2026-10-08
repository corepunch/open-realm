#!/usr/bin/env python3
"""Execute original request heap/clock code against frozen retail expectations."""
import argparse,json,subprocess,sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();assert not a.output.exists()
    subprocess.run([sys.executable,'tools/ghidra/research/verify_ORDER-05.1_request_heap.py',
        '--binary',str(a.binary),'--report',str(a.output),'--random','400',
        '--expected','tools/ghidra/fixtures/research/ORDER-05.1-expected.json'],check=True)
    r=json.loads(a.output.read_text());assert r['cases']==432 and r['mismatches']==0 and r['stats']['fault']==0
    r.update(passed=True,status='retail-agent-request-original',
        scope='Unmodified heap/queue/drain/cancel/rearm/clock instructions; deadline and unsigned serial keys, receiver callbacks and pool reuse.',
        exclusions=['Supplied two-clock owner, preallocated request blocks and controlled receiver bodies; Storm/CRT external services supplied.',
                    'Presentation requests and serial wrap are labelled forced-state oracle cases, not public gameplay witnesses.',
                    'Archive oracle report is not counted as fresh execution.'])
    a.output.write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':main()
