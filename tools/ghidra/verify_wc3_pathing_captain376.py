#!/usr/bin/env python3
"""Verify Captain preferred-primary and typed SuicideUnit producer parity."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
from wc3_pathfinding_engine_tests import run_engine
ROOT=Path(__file__).resolve().parents[2]
SPEC=Path(__file__).with_name('fixtures')/'retail-captain376-1.27.json'
def validate(spec,binary):
 assert spec['task']=='GROUP-03.4.7.3'and spec['payoff']==376
 for name,digest in spec['pins'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
 module_path=Path(__file__).with_name('research')/'captain376_native_replay.py'
 loader=importlib.util.spec_from_file_location('captain376_native_replay',module_path);module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module)
 native=module.verify(ROOT/spec['native_inputs'],binary)
 assert(native['observations'],native['controls'],native['family_control_comparisons'],native['instructions'],native['data_guards'])==(28,12,14,7801,16)
 return native

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--test-binary',type=Path,default=Path('build/bin/openwarcraft3-tests'));parser.add_argument('--data',type=Path,default=Path('build/tests'));parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
 spec=json.loads(SPEC.read_text());native=validate(spec,args.binary)
 engine=run_engine(args.test_binary,args.data,args.report,spec['patterns'])
 assert all(value==spec['engine_expected']for value in engine.values()),engine
 library=args.test_binary.resolve().parent.parent/'lib/libgame-wc3-test.so'
 result={'passed':True,'status':'verified','binary_sha256':native['binary_sha256'],'game_library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'captures':28,'controls':12,'family_control_comparisons':14,'instructions':7801,'data_guards':16,'native_records':sum(f['records']for f in native['families']),'public_markers':sum(f['public_markers']for f in native['families']),'engine':engine,'scope':spec['scope']}
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
