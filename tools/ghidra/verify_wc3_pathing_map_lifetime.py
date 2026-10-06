#!/usr/bin/env python3
"""Recheck original spatial/owner release and complete MAP-06.1 reload captures.

The isolated oracle constructs a spatial map and owner using original code,
retires every object, then releases both. It does not emulate a whole map load;
the retained read-only ChangeLevel/RestartGame captures certify that operation.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

HERE=Path(__file__).resolve().parent
FIXTURES=HERE/'fixtures'
BINARY_SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def original(binary):
    sys.path.insert(0,str(HERE/'research'))
    import sep03_map05_spatial_harness as H
    emu=H.Emu(binary)
    emu.call(0x6f003c40)
    cases=[]
    for cycle in range(9):
        baseline=set(emu.allocations)
        world=H.World(emu,8 if cycle<8 else 12,8,factory=True)
        objects=[world.create_object(world.make_mover()) for unused in range(3)]
        for obj in objects:
            world.update(obj,(2,2,4,4))
        world.update(objects[0],(2,5,4,7))
        world.update(objects[0],(2,2,4,4))
        assert world.query((2,2,4,4))==[objects[0],objects[2],objects[1]]
        request=world.fields()['timer_request']
        for obj in objects:
            world.retire(obj)
        assert all(emu.r(obj+0x38)==0xffffffff for obj in objects)
        before=world.fields()
        world.release_map()
        after=world.fields()
        assert after['records']==after['link_count']==after['link_capacity']==after['cells']==0
        assert after['timer_request']==0 and emu.r(request+0x10)&0x10000
        assert all(emu.r(obj+0x3c)==0 for obj in objects)
        # Spatial registration grows the external registry's pointer vector
        # (+4/+C). It has its own lifetime; owner destruction cannot free it.
        emu.call(0x6f1591e0,world.owner)
        retained=set(emu.allocations)-baseline
        assert retained=={emu.r(world.registry+4)}
        cases.append(dict(cycle=cycle,width=world.width,height=world.height,
            retired_objects=len(objects),records_before=before['records'],records_after=after['records'],
            links_after=after['link_capacity'],cells_after=after['cells'],
            request_cancelled=True,owner_allocations_remaining=0,
            external_registry_bytes=emu.allocations[emu.r(world.registry+4)]))
    return dict(binary_sha256=BINARY_SHA,cases=cases)


def captures():
    bundle=json.loads(gzip.decompress((FIXTURES/'retail-map-lifetime-inputs-1.27.json.gz').read_bytes()))
    expected_path=FIXTURES/'research/MAP-06.1-expected.json'
    expected=json.loads(expected_path.read_text())
    spec=importlib.util.spec_from_file_location('map_lifetime_composer',HERE.parent/'frida/research/sep03_map06_expected.py')
    composer=importlib.util.module_from_spec(spec);spec.loader.exec_module(composer)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        for name,text in bundle['files'].items():
            if Path(name).name!=name:raise ValueError('unsafe capture filename')
            raw=text.encode()
            if hashlib.sha256(raw).hexdigest()!=bundle['sha256'][name]:
                raise ValueError('retained capture differs: '+name)
            (root/name).write_bytes(raw)
        for name,digest in expected['captures'].items():
            if bundle['sha256'].get(name)!=digest:raise ValueError('frozen capture identity differs: '+name)
            rows=composer.rows_of(root/name)
            ends=[r for r in rows if r.get('event')=='trace-end']
            assert len(ends)==1 and ends[0]['installed'] and not ends[0]['caps']
            assert not [r for r in rows if r.get('event') in ('error','observer-error')]
        rebuilt=root/'rebuilt.json'
        composer.map061(*(root/name for name in expected['captures']),rebuilt)
        if rebuilt.read_bytes()!=expected_path.read_bytes():
            raise ValueError('complete frozen reload report differs')
        marker_count=0
        for scenario in ('changelevel_a-prechange','changelevel_b','restart-prerestart'):
            flow='restart' if scenario.startswith('restart') else 'changelevel'
            observed=composer.marker_lines(root/f'{flow}-observe-1-rs-{scenario}.txt')
            control=composer.marker_lines(root/f'{flow}-control-1-rs-{scenario}.txt')
            if not observed or observed!=control:raise ValueError('reload observer/control markers differ: '+scenario)
            marker_count+=len(control)
    restarts=expected['flows']['restart']['cycles']
    assert len(restarts)==9 and restarts[-1]['commits']==0
    releases=[r for flow in expected['flows'].values() for r in flow['cycles'] if r['spatial_release']]
    assert len(releases)==9
    for cycle in releases:
        assert cycle['equal_prefix_with_first']>=43
        assert cycle['teardown_order'][0]==['16eb83',1]
        assert sum(count for caller,count in cycle['teardown_order'] if caller.startswith('region:'))==158
        for spatial in cycle['spatial_release']:
            assert spatial['records_after']==spatial['link_capacity_after']==0
    change=expected['flows']['changelevel']['cycles']
    assert len(change)==2 and change[1]['equal_prefix_with_first']==46 and change[1]['commits']==111
    return dict(captured_files=len(bundle['files']),restart_cycles=8,releases=len(releases),
                control_markers=marker_count,changelevel_equal_prefix=46,
                frozen_sha256=hashlib.sha256(expected_path.read_bytes()).hexdigest())


def verify(binary,report):
    observed=original(binary)
    if observed!=json.loads((FIXTURES/'retail-map-lifetime-release-1.27.json').read_text()):
        raise ValueError('original owner/spatial release differs')
    result=dict(passed=True,binary_sha256=BINARY_SHA,original_cases=len(observed['cases']),**captures())
    report.write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(verify(args.binary.resolve(),args.report.resolve()),indent=2))
