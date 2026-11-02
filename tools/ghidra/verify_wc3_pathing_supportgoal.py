#!/usr/bin/env python3
"""Verify water/cliff/bridge route goals and destructable-target lifetimes against exact retail captures.

Family "support": five movement types (foot/fly/amph/hover/float) times four collision classes start
on the shallow bridge approach and receive ten public goals: deep water, the bridge deck, both dry
banks, the upper cliff, an outside point, a floating unit target (retained and removed), the bridge
destructable itself and a queryzero deep-water point (200 scenes).
Family "widget": the same twenty profiles target the bridge, which is killed (then restored) or
removed (then recreated) during the approach (40 scenes).
Each family has two read-only repeats and an observer-free control. The literal engine fixtures are
re-derived from the frozen original captures; they are not engine output.
"""
import argparse, base64, gzip, hashlib, json, re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-supportgoal-1.27.json'
BUNDLE = SPEC.with_suffix('.json.gz')
FAMILIES = {
    'support': dict(scenes=200, prefix='SG', task='FOOT-04.2/supportgoal', name='supportgoal',
                    header='games/warcraft-3/game/skills/fixtures/retail_supportgoal_motion.h'),
    'widget': dict(scenes=40, prefix='WG', task='FOOT-04.2/widgetgoal', name='widgetgoal',
                   header='games/warcraft-3/game/skills/fixtures/retail_widgetgoal_motion.h'),
}
TESTS = ['wc3_support.*', 'wc3_unit.*', 'wc3_movement.supportgoal*', 'wc3_movement.widgetgoal*', 'wc3_movement.bridge311*', 'wc3_movement.goal310*',
         'wc3_movement.place309*', 'wc3_movement.goal308*', 'wc3_movement.place308*', 'wc3_movement.foot307*',
         'wc3_fine_spatial.*', 'wc3_movement.target*', 'wc3_movement.follow*', 'wc3_movement.retail_gate_blocked_and_outside*',
         'wc3_destructable.*', 'wc3_order_lifecycle.*', 'wc3_save.rejects_prior_save_versions']
INPUTS = ['tools/frida/research/smart277_ui_input.c', 'tools/frida/research/point214_ui_input.c', 'tools/frida/wc3_ui_input.c']


def family_sources(f):
    return ['tools/frida/research/%s_%s' % (f['name'], s) for s in ('capture.py', 'observer.js', 'make_map.py', 'probe.j')]


SOURCES = sorted({p for f in FAMILIES.values() for p in family_sources(f) + [f['header']]} | set(INPUTS) | {'tools/ghidra/fixtures/retail-supportgoal-ghidra-1.27.json', 'tools/frida/research/widgetgoal_recovery_observer.js', 'tools/ghidra/fixtures/retail-supportgoal-recovery-inputs-1.27.json.gz', 'tools/ghidra/fixtures/retail-supportgoal-water-inputs-1.27.json.gz', 'tools/frida/research/widgetgoal_watergrid_observer.js', 'games/warcraft-3/game/tests/fixtures/retail_widgetgoal_water.h', 'tools/ghidra/research/supportgoal_ramp_oracle.py', 'tools/frida/research/preparedwater_ramp_observer.js', 'tools/frida/research/preparedwater_ramp_make_map.py', 'tools/ghidra/fixtures/retail-supportgoal-ramp-inputs-1.27.json.gz', 'tools/ghidra/fixtures/retail-supportgoal-ramp-kernels-1.27.json.gz', 'games/warcraft-3/game/tests/fixtures/retail_preparedwater_ramp.h', 'games/warcraft-3/game/tests/fixtures/retail_preparedwater_ramp_kernels.h', 'games/warcraft-3/game/tests/fixtures/retail_support_metadata.h', 'tools/ghidra/fixtures/retail-supportgoal-metadata-1.27.json.gz', 'tools/ghidra/research/supportgoal_metadata_oracle.py', 'games/warcraft-3/game/g_destructable.c', 'games/warcraft-3/game/g_local.h', 'games/warcraft-3/game/g_support.c', 'games/warcraft-3/game/g_world.c', 'games/warcraft-3/game/m_unit.c', 'games/warcraft-3/game/skills/s_move.c', 'games/warcraft-3/game/tests/t_movement.c', 'tools/ghidra/verify_wc3_pathing_supportgoal.py'})


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def markers(text, prefix):
    return re.findall(r'call Preload\( "(' + prefix + r' [^"\r\n]*)" \)', text)


def normalize(value):
    """Erase process pointers only; every scalar word stays exact."""
    if isinstance(value, list):
        return [normalize(x) for x in value]
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in value.items()}
    if isinstance(value, str) and re.fullmatch('0x[0-9a-f]+', value):
        return value != '0x0'
    return value


def goal_motion(rows, count):
    scenes = []
    for scene in range(count):
        rr = [x for x in rows if x.get('scene') == scene]
        if sum(x['event'] == 'marker' and 'label=end-cleanup' in x['value'] for x in rr) != 1:
            raise ValueError('incomplete scene %d' % scene)
        binds = [x for x in rr if x['event'] == 'bind']
        mid = binds[0]['m']['id']
        if any(x['m']['id'] != mid for x in binds):
            raise ValueError('foreign physical binding')
        before = [x for x in rr if x['event'] == 'commit-enter' and x['g']['count'] == 1 and x['g']['members'][0]['id'] == mid]
        after = [x for x in rr if x['event'] == 'commit-leave' and x['g']['count'] == 1 and x['g']['members'][0]['id'] == mid]
        if len(before) != len(after) or not before:
            raise ValueError('unpaired commits')
        data = []
        for a, b in zip(before, after):
            if a['c'] != b['c']:
                raise ValueError('unpaired counter')
            m = a['g']['members'][0]['m']; out = b['g']['members'][0]['m']
            data.append([a['c'], *out['clock'], *m['pos'], *m['vel'], m['range'], m['heading'], *out['pos'], *out['vel'], out['heading'], a['tick'], a['g']['flags']])
        fine = next((x for x in rr if x['event'] == 'fine-leave'), None)
        removal = next((x for x in rr if x['event'] == 'marker' and 'label=before-removal' in x['value']), None)
        final = next(x for x in rr if x['event'] == 'marker' and 'label=begin-cleanup' in x['value'])
        final_order = int(re.search(r'order=(\d+)', final['value'])[1])
        poses = []
        for x in rr:
            if x['seq'] >= final['seq']:
                break
            if x['event'] == 'recover-leave' and x['m']['id'] == mid:
                poses.append((x['seq'], x['m']['pos']))
            if x['event'] == 'commit-leave' and x['g']['count'] == 1 and x['g']['members'][0]['id'] == mid:
                poses.append((x['seq'], x['g']['members'][0]['m']['pos']))
        retries = [[x['c'], x['result']] for x in rr if x['event'] == 'retry-leave']
        scenes.append(dict(clock=binds[0]['m']['clock'], counter=binds[0]['c'], rows=data, binds=len(binds),
                           fine=dict({k: fine[k] for k in ('result', 'points', 'index', 'flags')},
                                     row=sum(x['seq'] < fine['seq'] for x in before)) if fine else None,
                           removal=dict(clock=removal['clock'][:2], c=removal['c']) if removal else None,
                           removal_index=sum(x['seq'] < removal['seq'] for x in before) if removal else 0,
                           final_order=final_order, final=dict(clock=final['clock'][:2], c=final['c']),
                           finalpose=max(poses)[1], retries=retries))
    return scenes


def render_header(scenes, name):
    w = lambda xs: ','.join('0x%08xu' % x for x in xs)
    header = ['/* Exact retail %s physical states and retry results; literal raw capture words. */' % name, '#pragma once']
    for i, s in enumerate(scenes):
        header += ['static uint32_t const %s_motion%d[][16]={' % (name, i)] + [' {' + w(row) + '},' for row in s['rows']] + ['};']
        if s['fine']:
            header += ['static uint32_t const %s_route%d[][2]={' % (name, i)] + [' {' + w(row) + '},' for row in s['fine']['points']] + ['};']
        if s['retries']:
            header += ['static uint32_t const %s_retry%d[][2]={' % (name, i)] + [' {%d,%d},' % tuple(r) for r in s['retries']] + ['};']
    header += ['static struct {uint32_t clock[2],counter;uint32_t const (*rows)[16];unsigned count;uint32_t const (*route)[2];unsigned route_count,index,route_row;'
               'uint32_t removal_clock[2],removal_counter;unsigned removal_index;uint32_t final_order,final_clock[2],final_counter,final_pose[2];'
               'uint32_t const (*retries)[2];unsigned retry_count,binds;} const %s_motion[]={' % name]
    for i, s in enumerate(scenes):
        fine, rem = s['fine'], s['removal']
        header += [' {{' + w(s['clock']) + '},' + str(s['counter']) + ',%s_motion%d,%d,' % (name, i, len(s['rows'])) +
                   ('%s_route%d,%d,%d,%d' % (name, i, len(fine['points']), fine['index'], fine['row']) if fine else 'NULL,0,0,0') +
                   (',{' + w(rem['clock']) + '},' + str(rem['c']) if rem else ',{0,0},0') + ',%d,%d' % (s['removal_index'], s['final_order']) +
                   ',{' + w(s['final']['clock']) + '},' + str(s['final']['c']) + ',{' + w(s['finalpose']) + '},' +
                   ('%s_retry%d,%d' % (name, i, len(s['retries'])) if s['retries'] else 'NULL,0') + ',%d},' % s['binds']]
    header += ['};']
    return '\n'.join(header) + '\n'


def summary(motion):
    retry = Counter(r[1] for s in motion for r in s['retries'])
    return dict(scenes=len(motion), motion_states=sum(len(s['rows']) for s in motion),
                first_route_points=sum(len(s['fine']['points']) for s in motion if s['fine']),
                successor_binds=sum(s['binds'] - 1 for s in motion),
                retry_results={str(k): v for k, v in sorted(retry.items())},
                final_orders=dict(sorted(Counter(str(s['final_order']) for s in motion).items())))


def validate_family(key, spec, caps):
    fam, fspec = FAMILIES[key], spec['families'][key]
    if len(caps) != 3 or [c['rows'][0]['mode'] for c in caps] != ['observe', 'observe', 'control']:
        raise ValueError('two repeats and unhooked control required')
    streams, motions = [], []
    for cap in caps:
        rows = cap['rows']; meta = rows[0]; footer = rows[-1]; public = markers(cap['preload'], fam['prefix'])
        if meta['task'] != fam['task'] or meta['sha256'] != SHA or not meta['owned'] or meta['env'] not in ('B', 'C') or \
                meta['display'] != {'B':':98', 'C':':99'}.get(meta['env']) or meta['source_sha256']['map'] != fspec['map_sha256'] or \
                footer['event'] != 'preload-file' or not footer.get('complete') or public != fspec['public'] or \
                footer['markers'] != len(public) or hashlib.sha256(cap['preload'].encode()).hexdigest() != footer['sha256'] or \
                any(x.get('type') == 'error' or x.get('event') == 'trace-failed' for x in rows):
            raise ValueError('capture provenance or public control')
        for p in family_sources(fam) + INPUTS:
            if meta['source_sha256'][Path(p).name] != spec['pins'][p]:
                raise ValueError('captured source differs')
        live = [x for x in rows if 'seq' in x and x['event'] != 'trace-end']
        if meta['mode'] == 'control':
            if live or any(x['event'] == 'trace-end' for x in rows):
                raise ValueError('hooked control')
            continue
        ends = [x for x in rows if x['event'] == 'trace-end']
        if len(ends) != 1 or not ends[0]['installed'] or not ends[0]['readOnly'] or ends[0]['seq'] != len(live) or \
                len(live) != fspec['native_records'] or [x['seq'] for x in live] != list(range(len(live))) or \
                dict(Counter(x['event'] for x in live)) != ends[0]['counts']:
            raise ValueError('lost native observer events')
        if [x['value'] for x in live if x['event'] == 'marker' and 'label=start' not in x['value']] != public:
            raise ValueError('live/preload marker mismatch')
        motions.append(goal_motion(rows, fam['scenes']))
        streams.append(normalize(live))
    if streams[0] != streams[1] or motions[0] != motions[1]:
        raise ValueError('repeated native streams differ')
    if (ROOT / fam['header']).read_text() != render_header(motions[0], fam['name']):
        raise ValueError('literal engine states differ from retail')
    result = summary(motions[0])
    if result != fspec['summary']:
        raise ValueError('frozen scene summary differs')
    return dict(captures=2, controls=1, public_markers=len(fspec['public']), native_records=fspec['native_records'], **result)



RECOVERY_INPUTS = 'tools/ghidra/fixtures/retail-supportgoal-recovery-inputs-1.27.json.gz'
RECOVERY_OBSERVER = 'tools/frida/research/widgetgoal_recovery_observer.js'
RECOVERY_FIELDS = {'mask','limit','callback','context','policy'}


def recovery_core(rows):
    result = []
    for row in rows:
        if 'seq' not in row or row['event'] == 'trace-end':
            continue
        copy = dict(row)
        if row['event'] == 'recover-enter':
            for field in RECOVERY_FIELDS:
                copy.pop(field)
        result.append(copy)
    return normalize(result)


def validate_recovery(spec, original, supplied=None):
    if spec.get('path') != RECOVERY_INPUTS or spec.get('recoveries') != 345:
        raise ValueError('recovery input contract differs')
    raw = (ROOT / RECOVERY_INPUTS).read_bytes()
    if hashlib.sha256(raw).hexdigest() != spec['sha256']:
        raise ValueError('recovery input bytes differ')
    bundle = supplied if supplied is not None else json.loads(gzip.decompress(raw))
    if bundle.get('version') != 1 or set(bundle['cases']) != {'observe_a','observe_b'}:
        raise ValueError('missing recovery repeat')
    reference = normalize([r for r in original[0]['rows'] if 'seq' in r and r['event'] != 'trace-end'])
    reference_public = markers(original[2]['preload'], 'WG')
    projections = []
    for case in bundle['cases'].values():
        capture = base64.b64decode(case['capture'], validate=True)
        preload = base64.b64decode(case['preload'], validate=True)
        rows = [json.loads(line) for line in capture.splitlines()]
        meta, footer = rows[0], rows[-1]
        if meta.get('mode') != 'observe' or meta.get('task') != 'FOOT-04.2/widgetgoal' or meta.get('sha256') != SHA or not meta.get('owned') or \
                meta.get('display') != {'B':':98','C':':99'}.get(meta.get('env')) or \
                meta['source_sha256']['map'] != original[0]['rows'][0]['source_sha256']['map'] or \
                footer.get('event') != 'preload-file' or footer.get('complete') is not True or footer['markers'] != 1907 or \
                footer['sha256'] != hashlib.sha256(preload).hexdigest() or markers(preload.decode(), 'WG') != reference_public or \
                any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
            raise ValueError('recovery observation provenance/control differs')
        sources = family_sources(FAMILIES['widget']) + INPUTS + [RECOVERY_OBSERVER]
        for path in sources:
            if path.endswith('/widgetgoal_observer.js'):
                continue
            if meta['source_sha256'].get(Path(path).name) != digest(ROOT / path):
                raise ValueError('recovery captured source differs')
        live = [r for r in rows if 'seq' in r and r['event'] != 'trace-end']
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if len(ends) != 1 or not ends[0].get('installed') or not ends[0].get('readOnly') or \
                ends[0]['seq'] != len(live) or [r['seq'] for r in live] != list(range(len(live))) or \
                ends[0]['counts'] != dict(Counter(r['event'] for r in live)) or recovery_core(rows) != reference:
            raise ValueError('recovery core stream differs from preserved observation')
        projection = []
        for row in live:
            if row['event'] != 'recover-enter':
                continue
            context = row['context']
            if context is not None and (not isinstance(context, list) or len(context) != 4):
                raise ValueError('recovery callback context missing')
            #654060 dereferences only the first context word. The three later
            #stack words remain literal in the raw capture, outside its input.
            projection.append([row['scene'],row['c'],row['mask'],row['limit'],row['callback'],context[0] if context else None,row['policy']])
        if len(projection) != spec['recoveries'] or projection != spec['contracts']:
            raise ValueError('recovery arguments/support level differ')
        projections.append(projection)
    if projections[0] != projections[1]:
        raise ValueError('recovery argument repeats differ')
    return dict(observations=2, recoveries_per_observation=345, core_records_per_observation=len(reference),
                public_markers=1907, original_control_reused=True)



WATER_INPUTS = 'tools/ghidra/fixtures/retail-supportgoal-water-inputs-1.27.json.gz'
WATER_OBSERVER = 'tools/frida/research/widgetgoal_watergrid_observer.js'
WATER_HEADER = 'games/warcraft-3/game/tests/fixtures/retail_widgetgoal_water.h'


def validate_water(spec, original, supplied=None):
    if spec.get('path') != WATER_INPUTS or spec.get('journey_acceptance') is not False:
        raise ValueError('prepared-water diagnostic scope differs')
    raw = (ROOT / WATER_INPUTS).read_bytes()
    if hashlib.sha256(raw).hexdigest() != spec['sha256']:
        raise ValueError('prepared-water diagnostic bytes differ')
    bundle = supplied if supplied is not None else json.loads(gzip.decompress(raw))
    if bundle.get('version') != 1 or set(bundle['cases']) != {'a','b'}:
        raise ValueError('prepared-water repeat missing')
    grids = []
    hashes = []
    for case in bundle['cases'].values():
        capture = base64.b64decode(case['raw'], validate=True)
        if hashlib.sha256(capture).hexdigest() != case['sha256']:
            raise ValueError('prepared-water raw digest differs')
        hashes.append(case['sha256'])
        rows = [json.loads(line) for line in capture.splitlines()]
        meta = rows[0]
        if meta.get('mode') != 'observe' or meta.get('task') != 'FOOT-04.2/widgetgoal' or meta.get('sha256') != SHA or not meta.get('owned') or \
                meta.get('display') != {'B':':98','C':':99'}.get(meta.get('env')) or meta.get('seconds') != 45 or \
                meta['source_sha256']['map'] != original[0]['rows'][0]['source_sha256']['map'] or \
                any(row.get('type') == 'error' or row.get('event') == 'trace-failed' for row in rows):
            raise ValueError('prepared-water diagnostic provenance differs')
        for path in family_sources(FAMILIES['widget']) + INPUTS + [WATER_OBSERVER]:
            if path.endswith('/widgetgoal_observer.js'):
                continue
            if meta['source_sha256'].get(Path(path).name) != digest(ROOT / path):
                raise ValueError('prepared-water diagnostic source differs')
        ends = [row for row in rows if row.get('event') == 'trace-end']
        live = [row for row in rows if 'seq' in row and row.get('event') != 'trace-end']
        if len(ends) != 1 or not ends[0].get('installed') or not ends[0].get('readOnly') or \
                ends[0]['seq'] != len(live) or [row['seq'] for row in live] != list(range(len(live))) or \
                ends[0]['counts'] != dict(Counter(row['event'] for row in live)):
            raise ValueError('prepared-water diagnostic event loss')
        found = [row for row in rows if row.get('event') == 'water-support-grid']
        if len(found) != 1 or found[0].get('width') != 16 or found[0].get('height') != 16 or \
                found[0].get('count') != 256 or found[0].get('waterTypeCount') != 1 or len(found[0].get('values',[])) != 256:
            raise ValueError('prepared-water grid missing')
        grids.append(found[0]['values'])
    if grids[0] != grids[1] or grids[0] != spec['values']:
        raise ValueError('prepared-water repeated words differ')
    header = (ROOT / WATER_HEADER).read_text()
    words = [int(word,16) for word in re.findall(r'0x([0-9a-f]{8})u',header)]
    if words != grids[0] or any(h not in header for h in hashes):
        raise ValueError('prepared-water literal header differs')
    return dict(observations=2, cells=256, journey_acceptance=False)



RAMP_INPUTS = 'tools/ghidra/fixtures/retail-supportgoal-ramp-inputs-1.27.json.gz'
RAMP_KERNELS = 'tools/ghidra/fixtures/retail-supportgoal-ramp-kernels-1.27.json.gz'
RAMP_HEADER = 'games/warcraft-3/game/tests/fixtures/retail_preparedwater_ramp.h'
RAMP_KERNEL_HEADER = 'games/warcraft-3/game/tests/fixtures/retail_preparedwater_ramp_kernels.h'
DATA_GUARDS = {'6fb77f84':'5046686f','6fb77f94':'80d7666f',
    '6fce67c4':'00000043','6fa8deac':'000000c0','6fcd53dc':'0000803e',
    '6fcd540c':'0000403f','6fcd53f8':'0000003f',
    '6fb86be0':'00000000000000000000000002000000',
    '6fb86bf0':'02000000000000000200000002000000','6fb80780':'b0f36e6f'}


def validate_ramp(spec, supplied=None, kernels=None):
    from research.supportgoal_ramp_oracle import cases, render, render_fields
    if spec.get('path') != RAMP_INPUTS or spec.get('journey_acceptance') is not False or spec.get('kernel_cases') != 28416:
        raise ValueError('ramp input scope differs')
    if digest(ROOT / RAMP_INPUTS) != spec['sha256'] or digest(ROOT / RAMP_KERNELS) != spec['kernel_sha256']:
        raise ValueError('ramp frozen input bytes differ')
    bundle = supplied if supplied is not None else json.loads(gzip.decompress((ROOT / RAMP_INPUTS).read_bytes()))
    if bundle.get('version') != 1 or set(bundle['cases']) != {'a','b'}:
        raise ValueError('ramp producer repeat missing')
    map_bytes = base64.b64decode(bundle['map']['raw'], validate=True)
    if hashlib.sha256(map_bytes).hexdigest() != bundle['map']['sha256'] or bundle['map']['sha256'] != spec['map_sha256']:
        raise ValueError('ramp map bytes differ')
    streams=[];grids=[]
    for case in bundle['cases'].values():
        raw = base64.b64decode(case['raw'], validate=True)
        if hashlib.sha256(raw).hexdigest() != case['sha256']:
            raise ValueError('ramp raw digest differs')
        rows = [json.loads(line) for line in raw.splitlines()]
        meta=rows[0]
        if meta.get('mode') != 'observe' or meta.get('task') != 'FOOT-04.2/widgetgoal' or meta.get('sha256') != SHA or not meta.get('owned') or \
                meta.get('display') != {'B':':98','C':':99'}.get(meta.get('env')) or meta.get('seconds') != 45 or \
                meta['source_sha256']['map'] != spec['map_sha256'] or \
                any(row.get('type')=='error' or row.get('event')=='trace-failed' for row in rows):
            raise ValueError('ramp diagnostic provenance differs')
        for path in family_sources(FAMILIES['widget']) + INPUTS + ['tools/frida/research/preparedwater_ramp_observer.js']:
            if path.endswith('/widgetgoal_observer.js'):continue
            if meta['source_sha256'].get(Path(path).name) != digest(ROOT / path):
                raise ValueError('ramp diagnostic source differs')
        live=[row for row in rows if 'seq' in row and row.get('event')!='trace-end']
        ends=[row for row in rows if row.get('event')=='trace-end']
        if len(ends)!=1 or not ends[0].get('installed') or not ends[0].get('readOnly') or \
                ends[0]['seq']!=len(live) or [row['seq'] for row in live]!=list(range(len(live))) or \
                ends[0]['counts']!=dict(Counter(row['event'] for row in live)):
            raise ValueError('ramp diagnostic event loss')
        selected=[{k:v for k,v in row.items() if k!='seq'} for row in live if row['event']!='marker']
        if Counter(row['event'] for row in selected)!={'water-build-enter':1,'water-build-leave':1,'ground-corner':1024,'ramp-offset':1024}:
            raise ValueError('ramp producer records missing')
        entry=next(row for row in selected if row['event']=='water-build-enter')
        if entry['width']!=16 or entry['height']!=16 or entry['waterTypes']!=1 or len(entry['vertices'])!=289:
            raise ValueError('ramp packed map input differs')
        streams.append(selected);grids.append(next(row['values'] for row in selected if row['event']=='water-build-leave'))
    if streams[0]!=streams[1] or grids[0]!=spec['values']:
        raise ValueError('ramp producer repeats differ')
    header=(ROOT / RAMP_HEADER).read_text()
    if [int(word,16) for word in re.findall(r'0x([0-9a-f]{8})u',header)]!=grids[0] or any(case['sha256'] not in header for case in bundle['cases'].values()):
        raise ValueError('ramp producer literal header differs')
    data = kernels if kernels is not None else json.loads(gzip.decompress((ROOT / RAMP_KERNELS).read_bytes()))
    frozen=data['cases'];inputs=cases()
    if data['game_sha256']!=SHA or len(frozen)!=len(inputs) or [{k:row[k] for k in case} for row,case in zip(frozen,inputs)]!=inputs:
        raise ValueError('ramp original kernel inputs differ')
    if Counter(row['offset'] for row in frozen)!={0:27164,0x42800000:1249,0x42000000:2,0x42c00000:1}:
        raise ValueError('ramp original corner outcomes differ')
    fields=data['prepared_grids']
    indices={next(i for i,row in enumerate(frozen) if row['offset']==offset) for offset in (0x42000000,0x42c00000,0x42800000)}
    if {(row['index'],row['cap']) for row in fields}!={(index,cap) for index in indices for cap in (14,0,2)}:
        raise ValueError('ramp authored corner/cap cases missing')
    if len(fields)!=9 or {(row['cap'],row['maximum']) for row in fields}!={(14,1536),(0,-256),(2,0)} or \
            any(len(row['values'])!=36 or (row['maximum']+256)//128!=row['cap'] for row in fields) or \
            (ROOT / RAMP_KERNEL_HEADER).read_text()!=render(frozen)+render_fields(fields):
        raise ValueError('ramp original field/literal header differs')
    return dict(producer_observations=2, producer_records_per_observation=2050, kernel_inputs=len(frozen),
                authored_cap_fields=9, journey_acceptance=False)


def verify_ramp_original(binary):
    from research.supportgoal_ramp_oracle import original, prepared_grid, replay_capture
    data=json.loads(gzip.decompress((ROOT / RAMP_KERNELS).read_bytes()))
    if original(binary)!=data['cases']:raise ValueError('fresh original ramp kernels differ')
    for row in data['prepared_grids']:
        if prepared_grid(binary,data['cases'][row['index']],row['cap'])!=row['values']:
            raise ValueError('fresh original authored-cap producer differs')
    bundle=json.loads(gzip.decompress((ROOT / RAMP_INPUTS).read_bytes()));count=0
    for case in bundle['cases'].values():
        rows=[json.loads(line) for line in base64.b64decode(case['raw']).splitlines()]
        count+=replay_capture(binary,rows,14)
    return dict(kernel_inputs=len(data['cases']), authored_cap_fields=9, live_corner_results=count)



METADATA_INPUTS = 'tools/ghidra/fixtures/retail-supportgoal-metadata-1.27.json.gz'
METADATA_HEADER = 'games/warcraft-3/game/tests/fixtures/retail_support_metadata.h'


def validate_metadata(spec,supplied=None):
    from research.supportgoal_metadata_oracle import STORM_SHA,cases,render
    if spec.get('path')!=METADATA_INPUTS or spec.get('cases')!=677 or spec.get('fields')!=8 or spec.get('storm_sha256')!=STORM_SHA:
        raise ValueError('metadata producer scope differs')
    if digest(ROOT/METADATA_INPUTS)!=spec['sha256']:
        raise ValueError('metadata frozen bytes differ')
    data=supplied if supplied is not None else json.loads(gzip.decompress((ROOT/METADATA_INPUTS).read_bytes()))
    if data.get('storm_sha256')!=STORM_SHA or [row['text'] for row in data['cases']]!=cases() or \
            [row['text'] for row in data['fields']]!=['0e99999','-2.56E2','1.536e3','+1536','274877906944','1e400','-1e400','1536'] or \
            any(len(row['values'])!=36 for row in data['fields']) or (ROOT/METADATA_HEADER).read_text()!=render(data):
        raise ValueError('metadata original producer/literal header differs')
    return dict(cases=677,fields=8,storm_sha256=STORM_SHA)


def verify_metadata_original(binary,storm):
    from research.supportgoal_metadata_oracle import original
    ramps=json.loads(gzip.decompress((ROOT/RAMP_KERNELS).read_bytes()))['cases']
    data=json.loads(gzip.decompress((ROOT/METADATA_INPUTS).read_bytes()))
    if original(binary,storm,ramps)!=data:
        raise ValueError('fresh original Storm/SSE metadata producer differs')
    return dict(cases=677,fields=8,storm_sha256=data['storm_sha256'])


def expected_instructions():
    result = {}
    for name in ['retail-bridge311-1.27.json', 'retail-goal310-1.27.json', 'retail-supportgoal-ghidra-1.27.json']:
        for address, raw in json.loads((ROOT / 'tools/ghidra/fixtures' / name).read_text())['instructions'].items():
            if address in result and result[address] != raw:
                raise ValueError('conflicting original guard')
            result[address] = raw
    return result


def validate(spec, bundle=None):
    if spec['version'] != 1 or spec['task'] != 'FOOT-04.2' or spec['game_sha256'] != SHA or spec['engine_tests'] != TESTS or \
            spec['instructions'] != expected_instructions() or len(spec['instructions']) != spec['instruction_count'] or \
            set(spec['pins']) != set(SOURCES) or set(spec['families']) != set(FAMILIES):
        raise ValueError('supportgoal contract differs')
    for p, h in spec['pins'].items():
        if digest(ROOT / p) != h:
            raise ValueError('source pin ' + p)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('frozen captures changed')
    caps = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    if set(caps) != set(FAMILIES):
        raise ValueError('capture family missing')
    result = {key: validate_family(key, spec, caps[key]) for key in FAMILIES}
    result['recovery'] = validate_recovery(spec['recovery'], caps['widget'])
    result['prepared_water'] = validate_water(spec['prepared_water'], caps['widget'])
    result['ramp'] = validate_ramp(spec['ramp'])
    result['metadata'] = validate_metadata(spec['metadata'])
    if spec.get('data_guards') != DATA_GUARDS:
        raise ValueError('CUnit support virtuals differ')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--storm-binary', type=Path)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('fresh report required')
    spec = json.loads(SPEC.read_text()); evidence = validate(spec); original_bytes(a.binary, dict(spec['instructions'], **spec['data_guards']))
    ramp_original = verify_ramp_original(a.binary)
    metadata_original = verify_metadata_original(a.binary,a.storm_binary or a.binary.parent/'Storm.dll')
    import verify_wc3_pathing_work242 as engine
    engine.TESTS = TESTS
    result = dict(passed=True, status='verified', task=spec['task'], instructions=len(spec['instructions']), families=evidence, ramp_original=ramp_original, metadata_original=metadata_original,
                  engine=engine.run_engine(a.test_binary, a.data, a.report), binary_sha256=SHA,
                  game_library_sha256=digest(a.test_binary.parent.parent / 'lib/libgame-wc3-test.so'), limits=spec['limits'])
    a.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result))


if __name__ == '__main__':
    main()
