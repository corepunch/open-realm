#!/usr/bin/env python3
"""Consolidate five immutable accepted AEme bundles without filtering raw rows."""
import argparse,base64,collections,gzip,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_morph382 as v
sha=lambda raw:hashlib.sha256(raw).hexdigest()
b64=lambda raw:base64.b64encode(raw).decode()
NAMES=['native','public-removal-native','alias-native','pause-native','sleep-pause-native']
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();archive=a.archive;out=a.output
 if (out/v.SPEC).exists()or(out/v.BUNDLE).exists():p.error('fresh output required')
 originals={name:(archive/(name+'-inputs-frozen.json.gz')).read_bytes()for name in NAMES};parts={name:json.loads(gzip.decompress(raw))for name,raw in originals.items()};raw={}
 for part in parts.values():
  for name,data in part['raw'].items():
   if name in raw and raw[name]!=data:raise ValueError('conflicting original raw input')
   raw[name]=data
 # Preserve setup failures separately from the accepted family inventory.
 for stem in ['sleep-pause-0-observe-a','friendly-sleep-pause-0-observe-a','detected-sleep-pause-0-observe-a','fresh-caster-sleep-pause-0-observe-a']:
  for f in sorted(archive.glob(stem+'*')):
   if f.is_file():raw[str(f.relative_to(archive))]=b64(f.read_bytes())
 for letter in 'stuv':
  for pattern in ['source/payoff382'+letter+'*','Payoff382'+letter+'-*']:
   for f in sorted(archive.glob(pattern)):
    if f.is_file():raw[str(f.relative_to(archive))]=b64(f.read_bytes())
 captured=dict(parts['native']['sources']);ported=[]
 for name,data in raw.items():
  if not name.startswith('source/'):continue
  base=Path(name).name
  relative=('tools/ghidra/research/'if base.endswith('.java')else'tools/frida/research/')+base
  if relative in captured and captured[relative]!=data:raise ValueError('captured source conflict')
  captured[relative]=data
  content=base64.b64decode(data,validate=True)
  if base.endswith('_make_map.py')and b'PARENT=Path("/run/media/lofcz/ssd_external/GitHub/open-realm/tools/frida/research")'in content:
   content=content.replace(b'PARENT=Path("/run/media/lofcz/ssd_external/GitHub/open-realm/tools/frida/research")',b'PARENT=HERE');ported.append(relative)
  target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(content)
 bundle={'raw':raw,'map_input':parts['native']['map_input'],'captured_sources':captured,'controllers':parts['native']['controllers'],'original_bundles':{n:b64(data)for n,data in originals.items()}}
 proof=parts['sleep-pause-native']['native_guards'];target=out/v.PROOF;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(proof,indent=2)+'\n')
 pins=set(v.PINS)
 if not set(captured)<=pins:raise ValueError('captured source lies outside reviewed portable inventory')
 for name in pins:
  if not v.resolve(name,out).is_file():raise ValueError('missing declared source dependency: '+name)
 # Diagnostic sources are retained and pinned, not promoted to accepted behavior.
 spec={'version':1,'task':'SEP-01.2','game_sha256':v.SHA,'engine_tests':v.TESTS,'projection_contract':v.CONTRACT,'instructions':proof['instructions'],'data_guards':proof['data_guards'],'pins':{name:sha(v.resolve(name,out).read_bytes())for name in sorted(pins)},'base_sha256':sha(base64.b64decode(bundle['map_input'],validate=True)),'groups':{},'raw_sha256':{name:sha(base64.b64decode(data,validate=True))for name,data in raw.items()},'ported_builders':sorted(ported),'original_bundle_sha256':{n:sha(data)for n,data in originals.items()}}
 for family,(stem,mapbase,prefix,scenes,count)in v.FAMILIES.items():
  for setting in(0,1):
   group={'map_sha256':sha(v.raw(bundle,mapbase+str(setting)+'.w3m')),'cases':{}}
   for mode in ['observe-a','observe-b','control-a']:
    name=stem+str(setting)+'-'+mode+'.jsonl';rows=[json.loads(line)for line in v.raw(bundle,name).decode().splitlines()];case={'environment':rows[0]['env'],'source_sha256':rows[0]['source_sha256']}
    if mode.startswith('observe'):
     projection,ledger=v.project(rows);case.update(retirement_ledger=ledger,reported_exceptions=[r for r in rows if r['event']=='native-exception'])
     if mode=='observe-a':group['projection']=projection
    group['cases'][mode]=case
   spec['groups'][family+str(setting)]=group
 spec['captured_source_sha256']={name:sha(base64.b64decode(value,validate=True))for name,value in captured.items()}
 spec['controller_sha256']={name:sha(base64.b64decode(value,validate=True))for name,value in bundle['controllers'].items()}
 spec['limits']={'subtype':'AEme/BEme policy, timed/public inverse, detach and private clocks. Captured health scalar retained literally; no health bonus parity claim. Other Morph subclasses and Mount remain the existing finite SEP audit.','exceptions':'Two reported prestart system-exception rows per observation precede active markers and lie outside game.dll; full rows retained. Controls have complete identical public streams. Cause and instrumentation contribution are unproved; no exception-free or OS-unreachability claim.','projection':'Only guarded Unit128/12c object lifetimes normalize; concurrent aliases and observed last-holder-zero retirement are preserved. Every other scalar, stable identity, clock, sequence and public/exception value is literal.','save':'Engine WriteGame/ReadGame in one process reconstructs status pools, explicit class/source references and derived timer heaps. No fresh-process native save parity claim.','portable_builders':'Only the captured absolute PARENT source-root assignment relocates to HERE. Original builder bytes/manifests remain frozen; map bytes and all other manifest members rebuild exactly.'}
 v.runtime(bundle,spec)
 packed=gzip.compress((json.dumps(bundle,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=sha(packed)
 (out/v.BUNDLE).write_bytes(packed);(out/v.SPEC).write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(v.runtime(bundle,spec)))
if __name__=='__main__':main()
