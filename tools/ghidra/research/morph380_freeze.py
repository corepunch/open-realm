#!/usr/bin/env python3
"""Freeze complete original ChemicalRage observations/control without capture filtering."""
import base64,collections,gzip,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_morph380 as v
from wc3_morph374_lifetimes import project,CONTRACT
ARCHIVE=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/payoff380')
BASE=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/payoff323-working/evidence/Separation323g.w3m')
CONTROL=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ORDER-02.3/payoff263/diagnostic')
sha=lambda raw:hashlib.sha256(raw).hexdigest()
b64=lambda raw:base64.b64encode(raw).decode()
def main():
 if v.SPEC.exists()or v.BUNDLE.exists():raise SystemExit('fresh freeze required; preserve accepted raw assets')
 bundle={'groups':{},'manifests':{},'map_input':b64(BASE.read_bytes()),'controllers':{n:b64((CONTROL/n).read_bytes())for n in ['queue263-ui.exe','queue263-ui.exe.so']},'diagnostics':{name:b64((ARCHIVE/name).read_bytes())for name in [f'{setting}-observe-{take}{suffix}'for setting in ['0','1']for take in ['a','b']for suffix in ['.jsonl','-launch.log']]+['reported-system-exception-audit.json','observer-revision-b.json']}}
 spec={'version':1,'task':'SEP-01.2','game_sha256':v.SHA,'engine_tests':v.TESTS,'projection_contract':CONTRACT,'pins':{p:sha((v.ROOT/p).read_bytes())for p in v.PINS},'base_sha256':sha(BASE.read_bytes()),'groups':{}}
 spec['instructions'],spec['data_guards']=v.guards()
 for kind in ['ordinary','detach']:
  for setting in ['0','1']:
   key=kind+setting;stem='detach-'if kind=='detach'else'';mapname='Payoff380d-'if kind=='detach'else'Payoff380-';cases={}
   for name in ['observe_a','observe_b','control_a']:
    take={'observe_a':'a'if kind=='detach'else'c','observe_b':'b'if kind=='detach'else'd','control_a':'a'}[name]
    mode='observe'if name.startswith('observe')else'control'
    capture=ARCHIVE/(stem+setting+'-'+mode+'-'+take+'.jsonl');preload=capture.with_name(capture.stem+'-preload.txt')
    case={'name':capture.name,'capture':b64(capture.read_bytes()),'preload':b64(preload.read_bytes())}
    if name.startswith('observe'):case['retirement_ledger']=project(v.parse_rows(case))[1]
    cases[name]=case
   bundle['groups'][key]=cases;bundle['manifests'][key]=json.loads((ARCHIVE/(mapname+setting+'.json')).read_text())
   spec['groups'][key]={'capture_sha256':{name:sha(v.decode(case,'capture'))for name,case in cases.items()},'preload_sha256':{name:sha(v.decode(case,'preload'))for name,case in cases.items()},'map_sha256':sha((ARCHIVE/(mapname+setting+'.w3m')).read_bytes()),'projection':project(v.parse_rows(cases['observe_a']))[0],'environments':{name:v.parse_rows(case)[0]['env']for name,case in cases.items()}}
 spec['limits']={'exceptions':'Each observation retains two literal prestart system-exception rows outside game.dll, identical to prior375 signatures. Cause remains unproved; observer-free controls establish complete equivalent public streams but cannot exclude an instrumentation contribution. Handler returnsfalse; no exception-free or OS-fault-unreachability claim.','projection':'Only proven Unit128/12c pointer lifetimes canonicalized; all other state/public/clock/sequence/exception words literal.','subtype':'ChemicalRage ANcr only; no Metamorphosis AEme equivalence claim.'}
 spec['diagnostics']={name:sha(base64.b64decode(raw,validate=True))for name,raw in bundle['diagnostics'].items()}
 v.runtime(bundle,spec)
 raw=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=sha(raw)
 v.BUNDLE.write_bytes(raw);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(v.runtime(bundle,spec)))
if __name__=='__main__':main()
