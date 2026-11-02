#!/usr/bin/env python3
"""Original68c190 structure support predicate, independently of authored rawcode."""
import argparse,itertools,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu

def original(binary):
 e=Emu(binary);unit=e.fixture(0x100);rows=[]
 for building,mobile,override,mode in itertools.product(range(2),repeat=4):
  flags=(building<<16)|(mobile<<7)|(override<<27);e.w(unit+0x5c,flags)
  value=e.call(0x6f68c190,unit,mode)
  if e.esp_after!=8:raise ValueError('support predicate ABI')
  rows.append(dict(flags=flags,mode=mode,result=value))
 return rows

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh output required')
 rows=original(a.binary);a.report.write_text(json.dumps(rows,indent=2)+'\n');print('16 original structure support decisions')
if __name__=='__main__':main()
