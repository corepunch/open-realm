#!/usr/bin/env python3
"""Differential retail spatial records vs the production C implementation.

No retail instructions are replaced; the original harness supplies only Storm
storage. Compare every raw cell record/index, entire free list, dirty cells,
stamp and per-object reference count at each mutation, including block growth.
"""
import argparse
import ctypes as C
import itertools
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'research'))
import sep03_map05_spatial_harness as H
END=0xffffff
U=C.c_uint32

CAPTURE_SHA256={'spatial_ab-control-1-rs-spatial_ab.txt': '99ddf5a07b735e4134b49aa902e81e31c6f71f9acd7a6898c58f98e98344ab7a', 'spatial_ab-observe-1-rs-spatial_ab.txt': '5cb5eba94d29f7c14e9199bd1b2248fe1ec7afd2cda8a560566fcd407707f363', 'spatial_ab-observe-1.jsonl': 'f2723db56ffe728129c50282daad86453b068faf0ef8be57b64fa2821a02c307', 'spatial_ba-control-1-rs-spatial_ba.txt': 'bca1a2dfa4381e0d27378bf9aba32661b666d16a43f5ee90af5439d9acebd1dc', 'spatial_ba-observe-1-rs-spatial_ba.txt': '36103a43dbdf47cd25cff37c9659b2739da691cc4ea3a09210c5db378552a7b2', 'spatial_ba-observe-1.jsonl': 'f09a959b08a7190493aa9b001278fb197d0739d69e6f451c25d017d44ce1cc07'}
EXPECTED_SHA256={'MAP-05.1': '8d73801a22f62fe9f29c663734a03413dc6ffd810ff36c07b677627d0d2e54b9', 'SEP-03.1': 'fdc2e65af02d8bbf08c00b7bbf9ee727a80d19acb8e7a922d48a1bba8eb05161', 'SEP-03.2': '0b1a6583042f9b7819811d839a0527ca064544c2ba256670c850b3d5b81e4f39'}

def verify_captures(payload=None):
    if payload is None:payload=json.loads(gzip.decompress((HERE/'fixtures/retail-spatial-storage-inputs-1.27.json.gz').read_bytes()))
    assert set(payload['files'])==set(CAPTURE_SHA256),'capture file set'
    spec=importlib.util.spec_from_file_location('spatial_storage_analyzer',HERE.parent/'frida/research/sep03_map06_analyze.py')
    analyzer=importlib.util.module_from_spec(spec);spec.loader.exec_module(analyzer)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for name,value in payload['files'].items():
            assert Path(name).name==name,'unsafe capture path'
            raw=value.encode();digest=hashlib.sha256(raw).hexdigest()
            assert digest==payload['sha256'][name]==CAPTURE_SHA256[name],('capture identity',name)
            (root/name).write_bytes(raw)
        summaries={}
        for order,callbacks in [('ab',697),('ba',698)]:
            stem='spatial_'+order;summary=analyzer.spatial(root/f'{stem}-observe-1.jsonl')
            assert summary['complete'] and summary['complete_marker'] and not summary['errors']
            assert summary['pool_reuse']['lifo_prediction_holds'] and summary['pool_reuse']['from_recycled']==20
            for kind in ('proximity','fine'):
                cadence=summary['cadence'][kind]
                assert cadence['callbacks']==callbacks and cadence['capacity']==[131072] and cadence['growth']==[131072]
                assert not cadence['reset_seen'],'natural stamp reset not captured'
            markers=analyzer.compare_markers(root/f'{stem}-observe-1-rs-{stem}.txt',root/f'{stem}-control-1-rs-{stem}.txt')
            assert markers['equal'] and markers['a_markers']==181
            summaries[order]=dict(callbacks_per_map=callbacks,control_markers=181,recycled_allocations=20,
                capture_sha256=CAPTURE_SHA256[f'{stem}-observe-1.jsonl'])
        return summaries


def verify_saved_evidence(readback=None):
    if readback is None:readback=json.loads((HERE/'fixtures/retail-spatial-storage-ghidra-1.27.json').read_text())
    assert readback['binary_sha256']==H.SHA256 and readback['unsaved'] is False
    assert len(readback['rows'])==14 and all('Payoff135' in r['comment'] for r in readback['rows'])
    layouts={l['name']:l for l in readback['layouts']}
    assert layouts['WC3PathMapHeader']['length']==108,'shared base must not grow with the derived map'
    assert layouts['WC3SpatialRecord']['length']==8 and layouts['WC3SpatialMapPrefix']['length']==188
    fields={f['offset']:f['name'] for f in layouts['WC3SpatialMapPrefix']['fields']}
    assert fields=={0:'base',108:'link_table',152:'dirty_bitmap',168:'dirty_word_count',172:'free_link_head',
        176:'outstanding_records',180:'cell_visit_stamp',184:'maintenance_request'}
    for task,digest in EXPECTED_SHA256.items():
        assert hashlib.sha256((HERE/f'fixtures/research/{task}-expected.json').read_bytes()).hexdigest()==digest,('frozen expected',task)
    return dict(saved_functions=14,saved_layouts=len(layouts),frozen_expected_sha256=EXPECTED_SHA256)

def verify_fine_saved_evidence(readback=None):
    if readback is None:readback=json.loads((HERE/'fixtures/retail-fine-records-ghidra-1.27.json').read_text())
    assert readback['binary_sha256']==H.SHA256 and readback['unsaved'] is False
    assert len(readback['rows'])==15 and all('Payoff136' in row['comment'] for row in readback['rows'])
    layouts={row['name']:row for row in readback['layouts']}
    assert layouts['WC3PathMapHeader']['length']==108 and layouts['WC3SpatialMapPrefix']['length']==188
    field=next(f for f in layouts['WC3FineSearchPrefix']['fields'] if f['offset']==28)
    assert field['name']=='map' and field['datatype']=='WC3SpatialMapPrefix *32'
    return dict(fine_saved_functions=15)


def verify_allocation_evidence(binary):
    results={}
    for task,script in [('MAP-05.3','verify_MAP-05.3_allocation_failure.py'),('SEP-03.3','verify_SEP-03.3_spatial_growth_failure.py')]:
        expected=HERE/f'fixtures/research/{task}-expected.json'
        with tempfile.TemporaryDirectory() as directory:
            report=Path(directory)/'report.json'
            subprocess.run([sys.executable,str(HERE/'research'/script),'--binary',str(binary),
                '--report',str(report),'--expected',str(expected)],check=True,stdout=subprocess.DEVNULL)
            actual=json.loads(report.read_text());assert actual.pop('passed') is True
            assert actual==json.loads(expected.read_text())
            results[task]=actual
    cleanup=results['MAP-05.3']['maintenance_reclaims_metadata']
    assert cleanup['before']['records']==2100 and cleanup['after']['records']==36
    assert not cleanup['metadata_records_left'] and not cleanup['dead_or_removal_records_left']
    assert cleanup['callbacks']==1 and cleanup['free_plus_records_equals_high_water']
    growth=results['SEP-03.3']
    assert len(results['MAP-05.3']['allocation_census'])==28
    return dict(allocation_sites=28,metadata_before=2100,metadata_after=36,
        allocation_expected_equal=True,growth_expected_equal=True)

class Engine:
    def __init__(self,lib,width,height):
        self.lib=lib;self.width=width;self.height=height
        self.map=lib.records_create(width,height,4096);self.ids={}
    def create(self,name,flags=0):
        owner=len(self.ids);identity=self.lib.records_object(self.map,owner,flags)
        self.ids[name]=identity;return identity
    def update(self,name,rect):self.lib.records_update(self.map,self.ids[name],*rect)
    def retire(self,name):self.lib.records_retire(self.map,self.ids[name])
    def metadata(self,x,y,lo,hi):self.lib.records_metadata(self.map,x,y,(hi<<16)|lo)
    def compact(self,all=False):self.lib.records_compact(self.map,all)
    def fields(self):
        out=(U*9)();self.lib.records_fields(self.map,out)
        return dict(zip(('link_count','link_capacity','records','free_head','free_count','stamp','raw_objects','live_objects','blocks'),out))
    def link(self,index):
        out=(U*3)();self.lib.records_link(self.map,index,out);return list(out)
    def chain(self,cell):
        index=self.lib.records_cell(self.map,cell);out=[]
        while index!=END:
            nxt,kind,payload=self.link(index);out.append([index,kind,payload]);index=nxt
        return out
    def free(self):
        out=[];index=self.fields()['free_head']
        while index!=END:
            out.append(index);index=self.link(index)[0]
        return out
    def query(self,rect):
        out=(U*4096)();n=self.lib.records_query(self.map,*rect,out);return [list(self.ids.values())[owner] for owner in out[:n]]
    def close(self):self.lib.records_free(self.map)


def library(directory):
    output=Path(directory)/'records.so'
    subprocess.run(['cc','-O2','-shared','-fPIC',str(HERE/'wc3_spatial_records_probe.c'),'-o',str(output)],check=True)
    lib=C.CDLL(str(output));lib.records_create.argtypes=[U,U,U];lib.records_create.restype=C.c_void_p
    types={'free':[], 'object':[U,U], 'update':[U,C.c_int,C.c_int,C.c_int,C.c_int],
        'retire':[U], 'metadata':[U,U,U], 'compact':[U], 'stamp':[U], 'object_stamp':[U,U],
        'fields':[C.POINTER(U)], 'cell':[U], 'dirty':[U], 'link':[U,C.POINTER(U)],
        'object_fields':[U,C.POINTER(U)], 'query':[C.c_int,C.c_int,C.c_int,C.c_int,C.POINTER(U)]}
    for name,args in types.items():
        f=getattr(lib,'records_'+name);f.argtypes=[C.c_void_p,*args];f.restype=U
    return lib


def compare(world,engine,names,objects=True):
    native=world.fields();actual=engine.fields()
    for name in ('link_count','link_capacity','records','free_head','stamp'):
        assert native[name]==actual[name],(name,native[name],actual[name])
    assert world.free_list()==engine.free(),'complete free-list order'
    dirty=[c for c in range(world.width*world.height) if engine.lib.records_dirty(engine.map,c)]
    assert world.dirty()==dirty,'dirty cells'
    for cell in range(world.width*world.height):
        n=[[i,k,names[p] if k!=2 else p] for i,k,p in world.chain(cell)]
        assert n==engine.chain(cell),('raw chain',cell,n,engine.chain(cell))
    if objects:
        for address,identity in names.items():
            out=(U*7)();engine.lib.records_object_fields(engine.map,identity,out)
            n=[world.e.r(address+0x3c)&END,world.e.r(address+0x38),world.e.r(address+0x40),*world.e.r(address+0x1c,4)]
            assert n==list(out),('object',identity,n,list(out))


def run(binary,lib):
    stages=queries=0
    for insert,remove in itertools.product(('PQ','QP'),repeat=2):
        e=H.Emu(binary);w=H.World(e,8,8);c=Engine(lib,8,8);names={};objs={}
        for name in insert+'R':
            objs[name]=w.create_object(w.make_mover());names[objs[name]]=c.create(name)
        for name,rect in [(insert[0],(2,2,4,4)),(insert[1],(2,2,5,5)),('R',(3,0,6,3)),
                          ('P',(2,2,4,5)),('P',(2,3,4,5)),('P',(2,2,4,5)),
                          ('R',(-2,-2,10,10)),('R',(2,2,5,5))]:
            w.update(objs[name],rect);c.update(name,rect);compare(w,c,names);stages+=1
            for query in ((0,0,8,8),(2,2,3,3),(-4,-4,-1,-1),(4,4,5,5)):
                assert [names[o] for o in w.query(query)]==c.query(query)
                compare(w,c,names);queries+=1
        for x,y,lo,hi in ((2,2,0x11,0x22),(3,3,0x33,0x44)):
            w.metadata(x,y,lo,hi);c.metadata(x,y,lo,hi);compare(w,c,names);stages+=1
        for name in remove:
            w.retire(objs[name]);c.retire(name);compare(w,c,names);stages+=1
        w.compact_dirty();c.compact();compare(w,c,names,False);stages+=1
        w.compact_all();c.compact(True);compare(w,c,names,False);stages+=1;c.close()
    # Original 131072-link crossing built solely by updates, not direct counts.
    e=H.Emu(binary);w=H.World(e,64,64);c=Engine(lib,64,64);names={};objs={}
    def create(name):
        objs[name]=w.create_object(w.make_mover());names[objs[name]]=c.create(name)
    def update(name,rect):w.update(objs[name],rect);c.update(name,rect)
    create('filler');flip=0
    while w.fields()['link_count']+512<=131054:
        update('filler',(0,0,16,16) if not flip else (32,32,48,48));flip^=1
    create('small');update('small',(60,0,61,1));pos=0
    while w.fields()['link_count']<131054:
        if 131054-w.fields()['link_count']==1:create('extra');update('extra',(63,3,64,4))
        else:pos^=1;update('small',(60,pos,61,pos+1))
    for name,rect in [('A',(20,20,22,22)),('B',(20,20,23,23)),('C',(19,19,25,25))]:
        create(name);update(name,rect);compare(w,c,names);stages+=1
    old=objs['C'];w.retire(old);c.retire('C');compare(w,c,names);stages+=1
    w.compact_dirty();c.compact();compare(w,c,names,False);stages+=1
    create('D');assert objs['D']==old and c.ids['D']==c.ids['C'];names[old]=c.ids['D']
    update('D',(19,19,25,25));compare(w,c,names);stages+=1
    assert [names[o] for o in w.query((18,18,26,26))]==c.query((18,18,26,26));queries+=1
    compare(w,c,names);boundary_capacity=c.fields()['link_capacity'];c.close()
    # Two original object blocks and LIFO recycling after last-record release.
    e=H.Emu(binary);w=H.World(e,8,8);c=Engine(lib,8,8);names={};objects=[]
    for i in range(65):
        name=str(i);o=w.create_object(w.make_mover());objects.append(o);names[o]=c.create(name)
        w.update(o,(1,1,2,2));c.update(name,(1,1,2,2))
    compare(w,c,names);stages+=1
    for i in (1,4,2):w.retire(objects[i]);c.retire(str(i))
    compare(w,c,names);stages+=1
    w.compact_dirty();c.compact();compare(w,c,names,False);stages+=1
    recycled=list(w.pool_state()['recycled'])
    for i in range(3):
        name=str(65+i);o=w.create_object(w.make_mover());identity=c.create(name)
        assert o==recycled[i] and identity==names[o]
        names[o]=identity;w.update(o,(1,1,2,2));c.update(name,(1,1,2,2))
    compare(w,c,names);stages+=1;c.close()
    # Labelled counter/object-stamp interventions at original mutation sites.
    for start in (0x7ffffffe,0x7fffffff,0x80000000,0xfffffffe,0xffffffff):
        for full in (False,True):
            e=H.Emu(binary);w=H.World(e,8,8);c=Engine(lib,8,8);names={};objs=[]
            for i in range(2):
                o=w.create_object(w.make_mover());objs.append(o);names[o]=c.create(str(i))
                w.update(o,(0,0,1,3) if not i else (0,0,1,1));c.update(str(i),(0,0,1,3) if not i else (0,0,1,1))
            w.update(objs[1],(0,1,1,2));c.update('1',(0,1,1,2))
            e.w(w.map+0xb4,start);lib.records_stamp(c.map,start)
            (w.compact_all if full else w.compact_dirty)();c.compact(full)
            compare(w,c,names);stages+=1;c.close()
    e=H.Emu(binary);w=H.World(e,8,8);c=Engine(lib,8,8)
    o=w.create_object(w.make_mover());identity=c.create('alias');names={o:identity}
    w.update(o,(1,1,2,2));c.update('alias',(1,1,2,2))
    e.w(o+0x38,15);lib.records_object_stamp(c.map,identity,15)
    e.w(w.map+0xb4,0x80000000);lib.records_stamp(c.map,0x80000000)
    w.compact_dirty();c.compact();compare(w,c,names);stages+=1
    e.w(w.map+0xb4,14);lib.records_stamp(c.map,14)
    for _ in range(2):
        assert [names[x] for x in w.query((1,1,2,2))]==c.query((1,1,2,2));compare(w,c,names);queries+=1
    w.metadata(1,1,0x11,0x22);c.metadata(1,1,0x11,0x22)
    e.w(o+0x38,1);lib.records_object_stamp(c.map,identity,1)
    e.w(w.map+0xb4,0x80000000);lib.records_stamp(c.map,0x80000000)
    w.compact_dirty();c.compact();compare(w,c,names);stages+=1;c.close()
    return dict(passed=True,status='verified',differences=[],binary_sha256=H.SHA256,mutation_stages=stages,queries=queries,
                boundary_capacity=boundary_capacity,
                compared='all raw chains, record indices, entire free lists, dirty cells, map/object stamps and references',
                exclusions=['Static region producers,49-link hierarchy cap and FOOT03 consumer matrix remain open.',
                            'Whole-owner invalid use after map release is diagnostic evidence, not an engine API.'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True);p.add_argument('--report',type=Path,required=True)
    args=p.parse_args()
    captures=verify_captures();evidence=verify_saved_evidence()
    fine_evidence=verify_fine_saved_evidence();allocation=verify_allocation_evidence(args.binary)
    with tempfile.TemporaryDirectory() as directory:result=run(args.binary,library(directory))
    result.update(live_captures=captures,ghidra=evidence,live_capture_count=len(captures),
        control_markers=sum(x['control_markers'] for x in captures.values()),saved_functions=evidence['saved_functions'])
    result.update(fine_evidence);result.update(allocation)
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
