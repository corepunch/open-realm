import gzip
import importlib.util
import json
from pathlib import Path
import unittest
import subprocess
import tempfile
import shutil
import signal
try:
    import resource
except ImportError:
    resource=None
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('storage_verifier',ROOT/'tools/ghidra/verify_wc3_pathing_records.py')
storage=importlib.util.module_from_spec(spec);spec.loader.exec_module(storage)
class SpatialStorageEvidence(unittest.TestCase):
    def test_complete_observed_captures_equal_controls(self):
        result=storage.verify_captures();self.assertEqual(sum(x['control_markers'] for x in result.values()),362)
    def test_truncated_capture_cannot_relabel_its_hash(self):
        data=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-inputs-1.27.json.gz').read_bytes()))
        name='spatial_ab-observe-1.jsonl';data['files'][name]=data['files'][name].rsplit('\n',2)[0]+'\n'
        data['sha256'][name]=storage.hashlib.sha256(data['files'][name].encode()).hexdigest()
        with self.assertRaises(AssertionError):storage.verify_captures(data)
    def test_saved_program_and_frozen_original_expectations(self):
        self.assertEqual(storage.verify_saved_evidence()['saved_functions'],14)
    def test_shared_header_must_remain_independent(self):
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-ghidra-1.27.json').read_text())
        next(l for l in data['layouts'] if l['name']=='WC3PathMapHeader')['length']=188
        with self.assertRaises(AssertionError):storage.verify_saved_evidence(data)
    def test_unsaved_annotations_do_not_certify_evidence(self):
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-ghidra-1.27.json').read_text());data['unsaved']=True
        with self.assertRaises(AssertionError):storage.verify_saved_evidence(data)
    def test_fine_saved_map_pointer_retains_shared_base(self):
        self.assertEqual(storage.verify_fine_saved_evidence()['fine_saved_functions'],15)
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-records-ghidra-1.27.json').read_text())
        field=next(f for l in data['layouts'] if l['name']=='WC3FineSearchPrefix' for f in l['fields'] if f['offset']==28)
        field['datatype']='WC3PathMapHeader *'
        with self.assertRaises(AssertionError):storage.verify_fine_saved_evidence(data)

    @unittest.skipUnless(shutil.which('cc') and resource,'requires native compiler and POSIX resource limits')
    def test_mid_update_allocation_failure_is_fatal_before_any_return(self):
        # Labelled allocator failure: production code executes unchanged; only
        # the external allocation is replaced, as in the original DLL witness.
        code=r"""
#include <stdlib.h>
#include <stdio.h>
static int fail;
static void *injected_realloc(void *p,size_t bytes) {
    if(fail){fprintf(stderr,"injected growth allocation %zu\n",bytes);return NULL;}
    return realloc(p,bytes);
}
#define realloc injected_realloc
#include "games/warcraft-3/common/wc3_pathing_records.h"
#undef realloc
int main(void) {
    wc3SpatialRecords_t map={0};wc3_records_init(&map,4,4,0);
    uint32_t id=wc3_records_create(&map,0,0);
    for(unsigned i=0;i<WC3_RECORD_GROWTH-8;i++)wc3_records_prepend(&map,0,i,WC3_RECORD_METADATA);
    fail=1;wc3_records_update(&map,id,(wc3FineBox_t){{0,0},{4,4}});
    fprintf(stderr,"returned partial membership\n");return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'failure.c';binary=Path(directory)/'failure';source.write_text(code)
            subprocess.run(['cc','-std=c11','-I',str(ROOT),str(source),'-o',str(binary)],check=True,capture_output=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True,
                preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
        self.assertEqual(result.returncode,-signal.SIGABRT)
        self.assertIn('injected growth allocation 2097152',result.stderr)
        self.assertNotIn('returned partial membership',result.stderr)
if __name__=='__main__':unittest.main()
