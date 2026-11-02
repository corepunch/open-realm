#!/usr/bin/env python3
"""Freeze complete original Submerge passive repeats and observer-free control."""
import argparse,base64,gzip,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_morph372 as v

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in('observe','repeat','control','base','map-manifest','controller'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('frozen outputs must be new')
    base=a.base.read_bytes()
    cases=[dict(rows=[json.loads(x)for x in path.read_text().splitlines()],preload=path.with_name(path.stem+'-preload.txt').read_bytes().decode())for path in(a.observe,a.repeat,a.control)]
    controllers={path.name:base64.b64encode(path.read_bytes()).decode()for path in(a.controller,Path(str(a.controller)+'.so'))}
    bundle=dict(map_input=base64.b64encode(base).decode(),map_manifest=json.loads(a.map_manifest.read_text()),controllers=controllers,cases=cases)
    spec=dict(version=1,task='SEP-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,base_sha256=v.digest(base),
        instructions=v.guard_inventory(),projection_contract=v.PROJECTION_CONTRACT,public=re.findall(r'call Preload\( "(M372 [^"\r\n]*)" \)',cases[0]['preload']),
        events=v.projection(cases[0]['rows']),capture_sources=[c['rows'][0]['source_sha256']for c in cases],
        map_sha256=cases[0]['rows'][0]['source_sha256']['map'],pins={name:v.digest((ROOT/name).read_bytes())for name in v.PIN_PATHS})
    print(v.runtime(bundle,spec))
    frozen=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0)
    spec['bundle_sha256']=v.digest(frozen);v.BUNDLE.write_bytes(frozen);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');v.validate(spec)
if __name__=='__main__':main()
