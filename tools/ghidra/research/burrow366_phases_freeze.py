#!/usr/bin/env python3
"""Freeze Burrow interruption originals before fixing early detach."""
import argparse,base64,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import wc3_burrow366_phases as v
R=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in('observe','repeat','control'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args()
 if v.SPEC.exists()or v.BUNDLE.exists():p.error('frozen outputs must be new')
 base=(R/'payoff323-working/evidence/Separation323g.w3m').read_bytes()
 cases=[dict(rows=[json.loads(x)for x in path.read_text().splitlines()],preload=path.with_name(path.stem+'-preload.txt').read_bytes().decode())for path in(a.observe,a.repeat,a.control)]
 bundle=dict(map_input=base64.b64encode(base).decode(),cases=cases)
 instructions={}
 for filename in('static-decoder.json','static-availability-final.json','static-detach-final.json'):instructions.update(json.loads((R/'burrow366'/filename).read_text())['instructions'])
 spec=dict(version=1,task='SEP-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,base_sha256=v.digest(base),instructions=instructions,
  public=re.findall(r'call Preload\( "(S366 [^"\r\n]*)" \)',cases[0]['preload']),events=v.projection(cases[0]['rows']),
  capture_sources=[c['rows'][0]['source_sha256']for c in cases],map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
 print(v.runtime(bundle,spec))
 parent=json.loads((ROOT/'tools/ghidra/fixtures/retail-burrow366-1.27.json').read_text())
 spec['pins']=parent['pins']|{path:v.digest((ROOT/path).read_bytes())for path in[
  'tools/frida/research/burrow366_phases_make_map.py','tools/frida/research/burrow366_phases_probe.j',
  'tools/frida/research/burrow366_availability_observer.js','tools/frida/research/separation366_capture.py','tools/frida/research/separation366_slow_capture.py','tools/frida/research/separation366_loaded_capture.py',
  'tools/ghidra/research/Burrow366AvailabilityEvidence.java','tools/ghidra/research/Burrow366DetachEvidence.java','tools/ghidra/research/burrow366_phases_freeze.py',
  'tools/ghidra/wc3_burrow366_phases.py']}
 frozen=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(frozen)
 v.BUNDLE.write_bytes(frozen);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');v.validate(spec)
if __name__=='__main__':main()
