#!/usr/bin/env python3
"""Fresh original subscriber-table execution for the strict parity corpus."""
import argparse,json,subprocess,sys
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 assert not a.output.exists()
 subprocess.run([sys.executable,'tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py',
  '--binary',str(a.binary),'--report',str(a.output),'--random','600',
  '--expected','tools/ghidra/fixtures/research/ORDER-03.1-expected.json'],check=True)
 report=json.loads(a.output.read_text());assert report['mismatches']==0 and report['cases']==627
 report.update(passed=True,status='retail-order-subscriber-original',scope='Unmodified register/unregister/dispatch/clear/growth/pool routines with supplied agents, callback bodies and external Storm/memset.',
  exclusions=['Controlled construction, not a public game producer.','Supplied handler bodies and external allocators; no game.dll instruction replacement.'])
 a.output.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
