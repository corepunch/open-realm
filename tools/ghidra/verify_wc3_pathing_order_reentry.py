#!/usr/bin/env python3
"""Fresh original nested subscriber execution, including cross-agent destruction."""
import argparse,gzip,json,subprocess,sys,tempfile
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();assert not a.output.exists()
    with tempfile.TemporaryDirectory(prefix='wc3-order-reentry-')as temp:
        expected=Path(temp)/'expected.json'
        expected.write_bytes(gzip.decompress(Path('tools/ghidra/fixtures/research/ORDER-03.2-expected.json.gz').read_bytes()))
        subprocess.run([sys.executable,'tools/ghidra/research/verify_ORDER-03.2_nested_lifetime.py',
            '--binary',str(a.binary),'--report',str(a.output),'--random','400','--expected',str(expected)],check=True)
    report=json.loads(a.output.read_text())
    assert report['cases']==411 and report['mismatches']==0 and report['stats']['fault']==0
    report.update(passed=True,status='retail-order-reentry-original',
        scope='Unmodified subscriber pools/dispatch/clear/refcount instructions; nested and cross-agent lifetime through depth four.',
        exclusions=['Controlled agents, callback bodies and external Storm/memset; no original instruction replacement.',
                    'Public RemoveUnit defers agent destruction; the forced oracle models its later clear/reference effects.'])
    a.output.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
