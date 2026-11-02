#!/usr/bin/env python3
"""Freeze completed CommonMorph374 repeats/controls; retain failed/shared-viewer diagnostics."""
import argparse,base64,gzip,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_morph374 as v
REQUIRED_DIAGNOSTICS={'default-observe-a.jsonl','viewer-0-observe-b.jsonl','0-observe-a.jsonl','1-observe-a.jsonl'}

def read_case(path,diagnostic=False):
    preload=path.with_name(path.stem+'-preload.txt')
    if not diagnostic and not preload.exists():raise ValueError('missing completed public preload: '+str(path))
    return dict(name=path.name,raw_jsonl=path.read_bytes().decode(),preload=preload.read_bytes().decode()if preload.exists()else None)

def prepare(args):
    settings={};manifests={}
    for setting,observe,repeat,control,manifest in args.case:
        if setting not in('default','0','1')or setting in settings:raise ValueError('unknown/duplicate setting')
        settings[setting]=[read_case(Path(path))for path in(observe,repeat,control)]
        manifests[setting]=json.loads(Path(manifest).read_text())
    if set(settings)!={'default','0','1'}:raise ValueError('complete default, zero and enabled setting triples required')
    diagnostics=[read_case(path,True)for path in args.diagnostic]
    if len({c['name']for c in diagnostics})!=len(diagnostics)or not REQUIRED_DIAGNOSTICS<={c['name']for c in diagnostics}:
        raise ValueError('retain original failed launches and shared-viewer diagnostics')
    controllers={path.name:base64.b64encode(path.read_bytes()).decode()for path in(args.controller,Path(str(args.controller)+'.so'))}
    base=args.base.read_bytes()
    bundle=dict(map_input=base64.b64encode(base).decode(),controllers=controllers,settings=settings,manifests=manifests,diagnostics=diagnostics)
    spec=dict(version=1,task='SEP-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,base_sha256=v.digest(base),
        instructions=v.guard_inventory(),vtable_slots=v.slot_inventory(),projection_contract=v.PROJECTION_CONTRACT,
        pins={name:v.digest((ROOT/name).read_bytes())for name in v.PIN_PATHS},settings={},
        diagnostics=[dict(name=c['name'],raw_sha256=v.digest(c['raw_jsonl'].encode()),
            preload_sha256=None if c['preload']is None else v.digest(c['preload'].encode()))for c in diagnostics])
    for key,cases in settings.items():
        first=v.whole_capture_projection(v.rows(cases[0]));second=v.whole_capture_projection(v.rows(cases[1]))
        spec['settings'][key]=dict(map_sha256=manifests[key]['map_sha256'],public=v.public(cases[0]['preload']),
            events=v.projection(v.rows(cases[0])),holder_ledgers=[v.project(v.rows(c))[1]for c in cases[:2]],
            whole_capture_allocator_variance_rows=sum(a!=b for a,b in zip(first,second)),
            capture_sha256=[v.digest(c['raw_jsonl'].encode())for c in cases])
    v.runtime(bundle,spec) # Never write accepted artifacts before every complete repeat/control passes.
    return bundle,spec

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--case',nargs=5,action='append',required=True,metavar=('SETTING','OBSERVE','REPEAT','CONTROL','MANIFEST'))
    p.add_argument('--base',type=Path,required=True);p.add_argument('--controller',type=Path,required=True)
    p.add_argument('--diagnostic',type=Path,action='append',required=True);a=p.parse_args()
    if v.SPEC.exists()or v.BUNDLE.exists():p.error('frozen outputs must be new')
    bundle,spec=prepare(a)
    frozen=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0)
    spec['bundle_sha256']=v.digest(frozen)
    v.BUNDLE.write_bytes(frozen);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');v.validate(spec)
    print(json.dumps(v.runtime(bundle,spec)))
if __name__=='__main__':main()
