#!/usr/bin/env python3
"""Verify mixed active/idle selected Shift admission and exact per-input journeys."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order, producer, request_rows, verify_neighbors


def verify_mixed_policy(actual, appended, neighbors, join, independent=False):
    variants = [r for r in actual if r['event'] == 'player-order-variant']
    actions = [r for r in actual if r['event'] == 'player-point-action-begin']
    if (len(variants) != 1 or variants[0]['target'] != [0xffffffff,0xffffffff] or
            len(actions) != 1 or (actions[0]['entry'],actions[0]['player'],actions[0]['flags'],actions[0]['order']) !=
            (0x6b9f70,3,9,851986)):
        raise ValueError('mixed input must be a target-free ground Shift Move')
    publications = [r for r in actual if r['event'] == 'player-order-publish']
    if len(publications) != 2 or any(r['flags'] != 9 or r['fallback'] for r in publications):
        raise ValueError('mixed point publication policy differs')
    if independent:
        if (not join or len(appended) != 2 or any(r['countBefore'] != 1 or r['countAfter'] != 2 or
                r['before'] != r['after'] or r['before'] == [-1,-1] for r in appended) or
                appended[0]['unit'] == appended[1]['unit'] or appended[0]['before'] == appended[1]['before']):
            raise ValueError('independent Shift must retain both distinct active heads')
        searches = verify_neighbors(neighbors,actions[0]['point'])
        if any(r['before']['category'] != 1 for r in searches):
            raise ValueError('independent ground cohort category differs')
        return searches
    if (len(appended) != 2 or [r['countBefore'] for r in appended] != [0,1] or
            [r['countAfter'] for r in appended] != [1,2] or
            appended[0]['before'] != [-1,-1] or appended[0]['after'] == [-1,-1] or
            appended[1]['before'] != appended[1]['after'] or appended[1]['before'] == [-1,-1] or
            appended[0]['unit'] == appended[1]['unit']):
        raise ValueError('mixed Shift must start the idle head and retain the active head')
    searches = [r for r in neighbors if r.get('event') == 'move-previous-cohort-search']
    if (len(searches) != 1 or searches[0]['result'] != int(join) or
            (searches[0]['output'] == '0x0') == join or
            searches[0]['point'] != actions[0]['point'] or searches[0]['before']['category'] != 1):
        raise ValueError('mixed completion cohort search differs')
    accepted = [r for r in neighbors if r.get('event') == 'move-previous-cohort-candidate' and r['accepted']]
    if len(accepted) != int(join):
        raise ValueError('mixed cohort accepted neighbor count differs')
    for r in accepted:
        if (r['result'] != 0 or r['candidate']['unit'] == r['source']['unit'] or
                r['candidate']['previous'] != r['source']['previous'] or
                r['source']['previous'] != searches[0]['before']['previous'] or
                r['request'] != searches[0]['output']):
            raise ValueError('mixed cohort neighbor identity differs')
    return searches


def verify_mixed(rows, case):
    actual = producer(rows)
    if actual != case['producer']:
        raise ValueError('mixed selection producer differs')
    return verify_mixed_policy(actual,
        [r for r in rows if r.get('event') == 'player-order-queued'][-2:],
        [r for r in rows if r.get('event','').startswith('move-previous-cohort')],case['joined'],case.get('independent',False))


def verify_lifecycle(rows, case):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if (len(metadata) != 1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')} != case['metadata'] or
            not metadata[0]['pointInput'].get('shift')):
        raise ValueError('mixed input provenance differs')
    if (len(ending) != 1 or not ending[0].get('installed') or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
        raise ValueError('mixed capture incomplete/failed')
    verify_mixed(rows,case)
    helpers = [r for r in rows if r.get('event') == 'player-input-helper']
    if (len(helpers) != 1 or 'down/up accepted' not in helpers[0]['output'] or
            helpers[0]['sha256'] != metadata[0]['source_sha256']['wc3-ui-input.exe']):
        raise ValueError('mixed owned native input helper differs')
    if digest(request_rows(rows)) != case['request_sha256'] or digest(canonical(rows)) != case['phases_sha256']:
        raise ValueError('mixed request or phase words differ')
    if owner_order(rows) != case['owner_order'] or motion_words(rows) != case['engine_motion']:
        raise ValueError('mixed owner order or motion words differ')
    for event,count in case['event_counts'].items():
        if sum(r.get('event') == event for r in rows) != count or ending[0]['counts'].get(event) != count:
            raise ValueError('mixed observer extent differs: '+event)
    markers = [r['value'] for r in rows if r.get('event') in ('marker','selected-marker')]
    if (not any('label=complete ' in r and r.endswith('order=0') for r in markers) or
            not any('PATHSELECT tick=300 ' in r and r.endswith('peerOrder=0') for r in markers)):
        raise ValueError('mixed user orders did not finish naturally')
    return dict(passed=True,joined=case['joined'],motion_sha256=digest(case['engine_motion']),
                natural_arrivals=case['event_counts']['task-arrival'],
                group_owner_passes=len(case['owner_order'])//4)


def render_header(fixture):
    out = '/* Original scene51: explicit external input clocks and complete per-capture motion. */\n'
    out += f'static uint32_t const selected_mixed_inputs[{len(fixture["cases"])}][4]={{\n'
    for case in fixture['cases']:
        r = next(r for r in case['producer'] if r['event'] == 'player-point-action-begin')
        out += '    {'+','.join(str(v)+'u' for v in [r['clock'][0],r['counter'],*r['point']])+'},\n'
    out += '};\n'
    for i,case in enumerate(fixture['cases']):
        out += f'static uint32_t const selected_mixed_motion_{i}[{len(case["engine_motion"])}][7]={{\n'
        for r in case['engine_motion']: out += '    {'+','.join(str(v)+'u' for v in r)+'},\n'
        out += '};\n'
    out += 'static struct { uint32_t const (*motion)[7]; unsigned count; } const selected_mixed_cases[]={\n'
    for i,c in enumerate(fixture['cases']): out += f'    {{selected_mixed_motion_{i},{len(c["engine_motion"])} }},\n'
    out += '};\n'
    if fixture.get('engine_prefix'):
        out = out.replace('selected_mixed',fixture['engine_prefix'])
        out = out.replace('Original scene51:',f'Original scene{fixture["scene"]}:')
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces',type=Path,nargs='+');p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--engine-library',type=Path,required=True);p.add_argument('--check-engine-header',type=Path)
    p.add_argument('--report',type=Path,required=True);args=p.parse_args()
    fixture=json.loads(args.fixture.read_text());engine=ctypes.CDLL(str(args.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(args.traces,fixture['cases'],strict=True):
        rows=[json.loads(s) for s in path.read_text().splitlines()];r=verify_lifecycle(rows,case)
        r.update(verify_motion(rows,engine,None));r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if args.check_engine_header and args.check_engine_header.read_text()!=render_header(fixture):
        raise ValueError('mixed engine header differs')
    report=dict(passed=True,cases=len(results),joined_cases=sum(r['joined'] for r in results),
                exact_decisions=sum(r['exact_decisions'] for r in results),
                exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),
                natural_arrivals=sum(r['natural_arrivals'] for r in results),
                group_owner_passes=sum(r['group_owner_passes'] for r in results),results=results,
                scope=fixture['scope'],fixture_sha256=hashlib.sha256(args.fixture.read_bytes()).hexdigest())
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
