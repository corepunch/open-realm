#!/usr/bin/env python3
"""Run the versioned retail corpus into fresh reports without redistributing assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

REPO=Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST=Path(__file__).with_name('fixtures')/'retail-pathfinding-corpus-1.27.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path):
    value=Path(path)
    if value.is_absolute() or '..' in value.parts or not value.parts:
        raise ValueError('unsafe relative corpus path: '+str(path))
    return value


def load_manifest(path,repo=REPO):
    manifest=json.loads(path.read_text())
    if manifest['version']!=1:raise ValueError('unsupported corpus version')
    entries=manifest['entries'];ids=set();reports=set();scripts=set()
    for entry in entries:
        if entry['id'] in ids or entry['report'] in reports:raise ValueError('duplicate corpus entry/report')
        ids.add(entry['id']);reports.add(entry['report']);relative(entry['report'])
        if entry['kind'] not in ('oracle','capture'):raise ValueError('unknown corpus entry kind')
        command=entry['command']
        if not isinstance(command,list) or not all(isinstance(v,str) for v in command):
            raise ValueError('corpus command must be an argument vector')
        script=relative(command[1]);source=repo/script
        if not source.is_file():raise ValueError('missing corpus script: '+str(script))
        if command[0]!='{python}' or '{report}' not in command:raise ValueError('missing command placeholders')
        if not entry['checks'] or not entry['scope']:raise ValueError('missing corpus result contract')
        if not set(entry['evidence'])<=set('SOCL'):raise ValueError('invalid evidence level')
        if entry['kind']=='oracle':
            scripts.add(str(script))
            if '{binary}' not in command:raise ValueError('oracle binary input missing')
            if entry['inputs']['binary_sha256']!=manifest['target']['game_sha256']:
                raise ValueError('oracle target hash differs')
        else:
            relative(entry['inputs']['capture'])
            if entry['inputs']['metadata']['sha256']!=manifest['target']['game_sha256']:
                raise ValueError('capture target hash differs')
            if entry['expected_status']=='archive-rejected' and entry['evidence']:
                raise ValueError('rejected capture cannot certify live evidence')
        if entry['expected_status']=='known-reference-difference':
            if entry['expected_exit']!=1 or not entry['checks'].get('differences',{}).get('length'):
                raise ValueError('known reference difference must remain an expected failure')
        if entry['expected_status']=='counterfactual-control' and not entry.get('intervention'):
            raise ValueError('counterfactual intervention must be explicit')
        for rule in entry['checks'].values():
            if not rule or not set(rule)<=set(('equal','length','minimum','nonempty')):
                raise ValueError('unknown report check')
    existing={str(p.relative_to(repo)) for p in (repo/'tools/ghidra').glob('verify_wc3_pathing_*.py')}
    if scripts!=existing:raise ValueError('oracle script inventory differs: '+str(sorted(scripts^existing)))
    for fixture in manifest['fixtures']:
        path=repo/relative(fixture['path'])
        if digest(path)!=fixture['sha256']:raise ValueError('fixture hash differs: '+fixture['path'])
    return manifest


def check_report(report,entry,target):
    if entry['kind']=='oracle':
        if report.get('binary_sha256')!=target['game_sha256']:raise ValueError('report binary hash differs')
        if report.get('passed',True) is not True:raise ValueError('oracle did not pass its own assertions')
        if report.get('mismatches'):raise ValueError('unexpected oracle mismatches')
        for key in ('crt_sha256',):
            if key in report and report[key]!=target['crt_sha256']:raise ValueError('report CRT hash differs')
    for field,rule in entry['checks'].items():
        if field not in report:raise ValueError('missing report field: '+field)
        value=report[field]
        if 'equal' in rule and (type(value)!=type(rule['equal']) or value!=rule['equal']):
            raise ValueError('report value differs: '+field)
        if 'length' in rule and len(value)!=rule['length']:raise ValueError('report length differs: '+field)
        if 'minimum' in rule and (type(value) is not int or value<rule['minimum']):
            raise ValueError('report count below minimum: '+field)
        if 'nonempty' in rule and bool(value)!=rule['nonempty']:raise ValueError('report emptiness differs: '+field)


def check_capture(path,entry):
    expected=entry['inputs']
    if digest(path)!=expected['sha256'] or path.stat().st_size!=expected['bytes']:
        raise ValueError('capture hash/length differs')
    rows=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    metadata=[r for r in rows if r.get('event')=='metadata']
    if len(metadata)!=1:raise ValueError('capture metadata missing/duplicated')
    actual={k:v for k,v in metadata[0].items() if k not in ('event','pid')}
    if actual!=expected['metadata']:raise ValueError('capture input metadata differs')
    ends=[r for r in rows if r.get('event')=='trace-end']
    if (len(ends)==1)!=entry['capture_complete']:raise ValueError('capture completion differs')
    if entry['capture_complete'] and not ends[0].get('installed'):
        raise ValueError('capture observer did not install')


def run_entry(entry,context,target):
    report=context['output']/relative(entry['report'])
    report.parent.mkdir(parents=True,exist_ok=True)
    values={k:str(v) for k,v in context.items()};values['report']=str(report)
    command=[arg.format_map(values) for arg in entry['command']]
    result=dict(id=entry['id'],kind=entry['kind'],expected_status=entry['expected_status'],
                expected_exit=entry['expected_exit'],command=command,report=str(report),verified_expected_status=False)
    start=time.monotonic()
    try:
        if report.exists():raise ValueError('refusing stale report')
        if entry['kind']=='capture':
            check_capture(context['archive']/entry['inputs']['capture'],entry)
            for extra in entry.get('additional_captures',[]):
                check_capture(context['archive']/relative(extra['inputs']['capture']),extra)
        with report.with_suffix('.log').open('w') as log:
            child=subprocess.run(command,cwd=REPO,stdout=log,stderr=subprocess.STDOUT,
                                 timeout=context['timeout'],check=False)
        result['exit']=child.returncode
        if child.returncode!=entry['expected_exit']:raise ValueError('unexpected process exit: '+str(child.returncode))
        if not report.is_file():raise ValueError('fresh report missing')
        observed=json.loads(report.read_text());check_report(observed,entry,target)
        result.update(verified_expected_status=True,report_sha256=digest(report),
                      evidence=entry['evidence'])
    except (OSError,ValueError,TypeError,KeyError,subprocess.TimeoutExpired) as error:
        result['failure']=str(error)
    result['seconds']=time.monotonic()-start
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,default=DEFAULT_MANIFEST)
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--archive',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--only',action='append',default=[],help='entry ID; repeat to select several')
    parser.add_argument('--timeout',type=float,default=1800,help='per-entry seconds')
    parser.add_argument('--check-only',action='store_true',help='validate inventory and repository fixture hashes')
    args=parser.parse_args();manifest=load_manifest(args.manifest)
    if args.timeout<=0:parser.error('timeout must be positive')
    unknown=set(args.only)-{e['id'] for e in manifest['entries']}
    if unknown:parser.error('unknown entry IDs: '+str(sorted(unknown)))
    selected=[e for e in manifest['entries'] if not args.only or e['id'] in args.only]
    if args.check_only:
        print(json.dumps(dict(entries=len(manifest['entries']),selected=len(selected),passed=True)))
        return 0
    if not args.output:parser.error('--output is required for execution')
    if args.output.exists():parser.error('output directory must not exist; refusing stale reports')
    needs_binary=any(e['kind']=='oracle' for e in selected)
    if needs_binary:
        if not args.binary:parser.error('--binary is required for original-code oracles')
        if digest(args.binary)!=manifest['target']['game_sha256']:parser.error('binary hash differs')
        crt=args.binary.with_name('msvcr120.dll')
        if digest(crt)!=manifest['target']['crt_sha256']:parser.error('sibling CRT hash differs')
    if any(e['kind']=='capture' for e in selected) and not args.archive:
        parser.error('--archive is required for capture audits')
    output=args.output.resolve();output.mkdir(parents=True)
    engine=output/'pathing-engine.so'
    build=None
    if any('{engine}' in e['command'] for e in selected):
        build=['cc','-O2','-shared','-fPIC','-MMD','-MF',str(output/'engine.d'),'-I',str(REPO),str(REPO/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(engine),'-lm']
        if any('--queue-composition' in e['command'] or '--stamp-wrap' in e['command'] or '--terrain-producer' in e['command'] or any(script in e['command'] for script in ('tools/ghidra/verify_wc3_pathing_storage.py','tools/ghidra/verify_wc3_pathing_adaptive_storage.py')) for e in selected): build.insert(1,'-DBZ_WC3_FINE_TRACE')
        subprocess.run(build,cwd=REPO,check=True)
    sources=set((REPO/'tools/ghidra').glob('*wc3_path*.py'))
    sources.update((REPO/'tools/frida').glob('*pathfinding*'))
    sources.update((REPO/'tools/frida').glob('wc3_captain*.ai'))
    sources.add(REPO/'tools/frida/wc3_blocker_lifecycle_probe.j')
    sources.add(REPO/'tools/frida/wc3_widget_overlap_probe.j')
    sources.update(REPO/entry['command'][1] for entry in manifest['entries'])
    sources.add(REPO/'tools/ghidra/generate_wc3_math_tables.py')
    sources.add(REPO/'tools/ghidra/wc3_shipped_crt.py')
    sources.update((REPO/'tools/ghidra').glob('MapPathfinding*.java'))
    sources.add(Path(__file__))
    if build:
        dependencies=(output/'engine.d').read_text().replace('\\\n',' ').split(':',1)[1].split()
        sources.update(Path(p).resolve() for p in dependencies)
    fingerprints={str(p.relative_to(REPO)):digest(p) for p in sorted(sources) if p.is_file()}
    context=dict(python=sys.executable,binary=args.binary.resolve() if args.binary else None,
                 archive=args.archive.resolve() if args.archive else None,output=output,engine=engine,timeout=args.timeout)
    results=[]
    for entry in selected:
        result=run_entry(entry,context,manifest['target']);results.append(result)
        print(entry['id']+': '+(entry['expected_status'] if result['verified_expected_status'] else 'UNEXPECTED '+result['failure']),flush=True)
        summary=dict(manifest_sha256=digest(args.manifest),target=manifest['target'],
                     source_sha256=fingerprints,
                     engine_build=build,engine_sha256=digest(engine) if engine.exists() else None,
                     completed=len(results),requested=len(selected),results=results,
                     passed=len(results)==len(selected) and all(r['verified_expected_status'] for r in results))
        (output/'corpus-results.json').write_text(json.dumps(summary,indent=2)+'\n')
    return int(not summary['passed'])


if __name__=='__main__':raise SystemExit(main())
