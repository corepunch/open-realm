#!/usr/bin/env python3
"""Summarize observed path searches, rejecting incomplete or inconsistent witnesses."""
import argparse
import collections
import hashlib
import json
import math
import re
from pathlib import Path


def check_pathing_toggle(rows, markers, violations):
    """Bracket public SetUnitPathing and distinguish own query from occupancy."""
    events = [r for r in rows if r.get('event') in ('pathing-toggle','movement-mask-publication')]
    native = [r for r in events if r['event']=='pathing-toggle']
    expected = [(flag, phase) for flag in (0,1,0,1) for phase in ('enter','leave')]
    if [(r.get('enabled'),r.get('phase')) for r in native] != expected:
        violations.append('pathing toggle lacks four complete ordered native brackets')
        return
    if len({r.get('handle') for r in native}) != 1 or not native[0].get('handle'):
        violations.append('pathing toggle changed native receiver')
    publications=[]
    for i in range(0,8,2):
        start,end=native[i:i+2]
        group=events[events.index(start)+1:events.index(end)]
        if len(group)!=1 or group[0].get('event')!='movement-mask-publication':
            violations.append('pathing toggle lacks unique bracketed mask publication')
            continue
        row=group[0]; mask=2 if start['enabled'] else 0
        if (row.get('rawcode'),row.get('category'),row.get('queryMask'),row.get('objectCategory'),row.get('pathMask')) != (1751543663,202,mask,0x010000ca,mask | mask<<24):
            violations.append('pathing toggle changed occupancy or published wrong query mask')
        publications.append(row)
    if len({(r.get('mover'),tuple(r.get('identity',[]))) for r in publications}) != 1:
        violations.append('pathing toggle changed mover identity')
    transitions=[m for m in markers if m['label'].startswith('pathing_')]
    wanted=[(tick,'pathing_'+name+'_'+phase) for tick,name in ((10,'disable'),(20,'enable'),(30,'disable'),(40,'enable')) for phase in ('before','after')]
    if [(m['tick'],m['label']) for m in transitions]!=wanted:
        violations.append('pathing toggle has missing or unordered script transitions')
    elif any((a['x'],a['y'],a['order'])!=(b['x'],b['y'],b['order']) for a,b in zip(transitions[::2],transitions[1::2])):
        violations.append('pathing native moved unit or replaced its order')
    crossing=[m for m in markers if m['label']=='sample' and 30<=m['tick']<40 and -576<=m['y']<-544]
    if not crossing or any(m['x']!=-1936 or m['order']!=851986 for m in crossing):
        violations.append('disabled pathing lacks active straight wall crossing')


def check_crowd_experiment(rows, violations):
    samples = collections.defaultdict(list)
    orders = []
    for row in rows:
        if row.get('event') != 'crowd-marker':
            continue
        value = row.get('value', '')
        match = re.fullmatch(r'PATHCROWD tick=(\d+) id=(\d+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', value)
        if match:
            tick, unit, x, y, order = match.groups()
            samples[int(unit)].append((int(tick), float(x), float(y), int(order)))
            continue
        match = re.fullmatch(r'PATHCROWD tick=10 id=(\d+) ordered=([01])', value)
        if match:
            orders.append(tuple(map(int, match.groups())))
        else:
            violations.append('malformed crowd marker')
    if set(samples) != set(range(9)) or any([s[0] for s in v] != list(range(1, 301)) for v in samples.values()):
        violations.append('crowd lacks 300 ordered samples for each of nine units')
    if sorted(orders) != [(n, 1) for n in range(9) if n != 4]:
        violations.append('crowd companion orders missing, rejected or duplicated')
    return {'units': len(samples), 'samples': sum(map(len, samples.values())),
            'final': {str(n): list(v[-1]) for n, v in samples.items() if v}}


def check_ground_crowd_arrivals(rows, crowd, violations):
    """Correlate this fixture's initial positions, arrival path and final samples."""
    first = {}
    final = {}
    for row in rows:
        if row.get('event') == 'arrival-transition':
            first.setdefault(row['mover'], row)
            if row.get('result') == 1:
                final[row['mover']] = row
    outcomes = []
    for unit in range(9):
        initial = (-2016 + unit % 3 * 80, -1056 + unit // 3 * 80)
        matches = [m for m, a in first.items() if
                   math.hypot(a['source'][0] * 32 - 7168 - initial[0],
                              a['source'][1] * 32 - 3072 - initial[1]) < 0.01]
        if len(matches) != 1 or matches[0] not in final:
            violations.append('ground crowd unit lacks unique initial mover and accepted arrival')
            continue
        mover = matches[0]
        arrival = final[mover]
        world = [arrival['source'][0] * 32 - 7168, arrival['source'][1] * 32 - 3072]
        sample = crowd['final'].get(str(unit))
        if not sample or math.hypot(world[0] - sample[1], world[1] - sample[2]) > 0.01 or sample[3] != 0:
            violations.append('ground crowd accepted arrival differs from final stopped unit')
        forced = bool(arrival.get('flags', 0) & 0x10000)
        linked = []
        if forced:
            for force in rows:
                if force.get('event') != 'force-arrival' or force.get('mover') != mover or force['ms'] > arrival['ms']:
                    continue
                if force.get('caller') != '0x16fd32' or (force.get('advance') or {}).get('result') != 4:
                    continue
                if any(r.get('event') == 'retry-result' and r.get('path') == force.get('path') and
                       r.get('counter') == force.get('counter') and r.get('result') == 4 and
                       r.get('before') == r.get('after') == 1 for r in rows):
                    linked.append(force)
            if not linked:
                violations.append('ground crowd forced arrival lacks linked retry exhaustion')
        terminal_search = None
        if linked:
            force = max(linked, key=lambda r: r['ms'])
            searches = [r for r in rows if r.get('event') == 'search' and r.get('kind') == 'fine' and
                        r.get('path') == force.get('path') and r.get('ms', 0) <= force['ms']]
            if searches:
                search = searches[-1]
                terminal_search = {k: search[k] for k in ('request', 'result', 'pops', 'budget')}
                terminal_search['blockers'] = search.get('blockers')
        distance = math.hypot(world[0] + 1936, world[1] + 144)
        outcomes.append({'unit': unit, 'mover': mover, 'forced': forced,
                         'retry_exhaustion_linked': bool(linked), 'terminal_search': terminal_search, 'world_position': world,
                         'goal_distance_world': distance, 'threshold_world': arrival['threshold'] * 32})
    return outcomes


def summarize_widgets(rows, violations):
    methods = [r for r in rows if r.get('event') == 'widget-method']
    counts = {name: sum(r.get('method') == name for r in methods)
              for name in ('create', 'destroy', 'remove-mask', 'reapply')}
    metadata = next((r for r in rows if r.get('event') == 'metadata'), {})
    final = next((r for r in rows if r.get('event') == 'trace-end'), None)
    if metadata.get('widgetEvents') and final:
        for name, count in counts.items():
            expected = min(final.get('counts', {}).get('widget-' + name, 0), metadata.get('samples', 200))
            if count != expected:
                violations.append('widget sample/count mismatch: ' + name)
    created = set()
    paired = []
    for row in methods:
        if row.get('method') == 'create' and row.get('afterCollection') not in (None, '0x0'):
            created.add((row.get('widget'), row['afterCollection']))
        if row.get('method') == 'destroy' and row.get('afterCollection') == '0x0':
            key = (row.get('widget'), row.get('beforeCollection'))
            if key in created:
                paired.append(dict(widget=key[0], collection=key[1]))
                created.remove(key)
    return dict(method_counts=counts, paired_create_destroy=paired)


def summarize_tasks(rows, violations):
    metadata = next((r for r in rows if r.get('event') == 'metadata'), {})
    final = next((r for r in reversed(rows) if r.get('event') == 'trace-end'), {})
    if metadata.get('taskEvents') and final:
        cap = metadata.get('samples', 0)
        for kind in ('point-task', 'task-arrival', 'task-prepend', 'task-cleanup',
                     'task-cant-path', 'task-recovery'):
            expected = min(final.get('counts', {}).get(kind, 0), cap)
            if sum(r.get('event') == kind for r in rows) != expected:
                violations.append('task sample/count mismatch: ' + kind)
    points = [r for r in rows if r.get('event') == 'point-task']
    accepted = 0
    for row in points:
        before, after = row.get('before', {}), row.get('after', {})
        identity = row.get('taskIdentity')
        if not isinstance(identity, list) or len(identity) != 2:
            violations.append('point task lacks identity')
            continue
        if before.get('ability') != after.get('ability') or before.get('unit') != after.get('unit'):
            violations.append('point task receiver changed')
        if row.get('eventCode') not in (0xd016b, 0xd016c, 0xd016d, 0xd016e):
            violations.append('unexpected point task event')
        if after.get('abilityFlags', 0) & 4 and after.get('taskHead') == identity:
            accepted += 1
            if after.get('unitFlags', 0) & 1:
                violations.append('accepted point task retained dispatch bit')
    prepends = [r for r in rows if r.get('event') == 'task-prepend']
    for row in prepends:
        identity = row.get('taskIdentity')
        if identity != [-1,-1] and row.get('afterHead') != identity:
            violations.append('registered prepended task differs from new head')
        successor = row.get('successor')
        if successor != [-1,-1] and successor != row.get('beforeHead'):
            violations.append('prepended task successor differs from prior head')
    arrivals = [r for r in rows if r.get('event') == 'task-arrival']
    active_cleanup = [r for r in rows if r.get('event') == 'task-cleanup'
                      and r.get('before', {}).get('abilityFlags', 0) & 4
                      and not (r.get('after', {}).get('abilityFlags', 0) & 4)]
    return {'active_cleanup_calls': len(active_cleanup),
            'active_cleanup_retained_head': sum(r['before'].get('taskHead') == r['after'].get('taskHead')
                                                for r in active_cleanup),
            'prepends' : len(prepends),
            'cleanup_calls': sum(r.get('event') == 'task-cleanup' for r in rows),
            'cant_path_calls': sum(r.get('event') == 'task-cant-path' for r in rows),
            'recovery_calls': sum(r.get('event') == 'task-recovery' for r in rows),
            'point_calls' : len(points), 'accepted': accepted,
            'other_outcomes': len(points)-accepted, 'arrival_calls': len(arrivals)}


def summarize_yielding(rows, violations):
    setters = [r for r in rows if r.get('event') == 'yield-set']
    delays = [r for r in rows if r.get('event') == 'path-delay']
    gaps, previous = collections.Counter(), {}
    for row in setters:
        if row['after'] != max(row['before'], row['requested']) or row['stored'] != row['identity']:
            violations.append('yield setter changed identity or countdown incorrectly')
    for row in delays:
        if row['before'] <= 0 or row['after'] != row['before'] - (0 if row['disabled'] else 1):
            violations.append('path delay countdown transition invalid')
        if row['result'] != (0x100000 if row['disabled'] else 1):
            violations.append('path delay return value invalid')
        prior = previous.get(row['path'])
        if prior and row['before'] == prior['after']:
            gaps[(row['counter'] - prior['counter']) & 0xffffffff] += 1
        previous[row['path']] = row
    return {'setters': len(setters), 'delayed_calls': len(delays),
            'requests': dict(collections.Counter(r['requested'] for r in setters)),
            'callers': dict(collections.Counter(r['caller'] for r in setters)),
            'consecutive_counter_gaps': dict(gaps)}


def summarize_blockers(rows, violations):
    requests = [r for r in rows if r.get('event') == 'route' and r.get('kind') == 'fine' and 'blockers' in r]
    totals = collections.Counter()
    movers = set()
    for request in requests:
        stats = request['blockers']
        if stats.get('unclassifiedHits', 0):
            violations.append('fine-cell rejection could not be classified')
        for key in ('objectHits', 'terrainHits', 'boundsHits', 'omittedHits'):
            if not isinstance(stats.get(key), int) or stats[key] < 0:
                violations.append('invalid blocker count')
            else:
                totals[key] += stats[key]
        objects = list(stats.get('objects', {}).values())
        if sum(o.get('hits', 0) for o in objects) + stats.get('omittedHits', 0) != stats.get('objectHits'):
            violations.append('blocker object counts do not reconcile')
        for obj in objects:
            if not (obj.get('objectMask', 0) & obj.get('queryMask', 0) & 0xffffff):
                violations.append('blocker lacks matching occupancy mask')
            if obj.get('flags', 0) & 0x8fffffff or (obj.get('mode') == 0 and obj.get('flags', 0) & 0x60000000):
                violations.append('blocker flags should have exempted this object')
            if obj.get('isMover'):
                movers.add(obj['payload'])
    return {'requests': len(requests), 'hits': dict(totals), 'movers': sorted(movers)}


def check_visibility_experiment(rows, violations):
    """Verify the observed Smart-follow cancellation experiment, not reacquisition."""
    positions, transitions, follower = [], [], []
    tick, active_targets = 0, {}
    group_destinations = []
    for row in rows:
        event = row.get('event')
        if event == 'marker':
            match = re.fullmatch(r'PATHTRACE tick=(\d+) label=sample x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', row.get('value', ''))
            if match:
                follower.append(dict(tick=int(match[1]), x=float(match[2]), y=float(match[3]), order=int(match[4])))
        elif event == 'target-marker':
            match = re.fullmatch(r'PATHTARGET tick=(\d+) x=(-?[\d.]+) y=(-?[\d.]+) visible=([01])', row.get('value', ''))
            if not match:
                violations.append('malformed visibility target sample')
                continue
            tick = int(match[1])
            positions.append(dict(tick=tick, x=float(match[2]), y=float(match[3]), visible=int(match[4])))
        elif event == 'group-target':
            active_targets[row.get('path')] = row.get('target')
        elif event == 'target-visibility' and active_targets.get(row.get('path')) == row.get('target'):
            transitions.append({**row, 'tick_before': tick})
        elif event == 'path-destination' and row.get('caller') == '0x16ce73' and active_targets.get(row.get('path')) not in (None, '0x0'):
            group_destinations.append({**row, 'tick_before': tick})
    if [p['tick'] for p in positions] != list(range(1, 301)):
        violations.append('visibility experiment lacks 300 ordered target samples')
    if any(p['x'] != -1936 or p['y'] != (-144 if p['tick'] < 80 else 112) for p in positions):
        violations.append('visibility experiment target does not match controlled jump')
    if any(p['visible'] != (0 if 80 <= p['tick'] < 160 else 1) for p in positions if p['tick'] < 60 or p['tick'] >= 80):
        violations.append('target was not hidden across its jump or visible in control phases')
    blocked = [r for r in transitions if r.get('blocked') == 1]
    if blocked:
        violations.append('Smart-follow cancellation fixture unexpectedly reached a blocked group callback')
    after_loss = [p for p in follower if p['tick'] >= 80]
    if not after_loss or any(p['order'] != 0 for p in after_loss):
        violations.append('Smart-follow did not remain cancelled after target visibility loss')
    if any(p['order'] != 851971 for p in follower if 10 <= p['tick'] < 60):
        violations.append('Smart-follow was not active in the visible control phase')
    if after_loss and any((p['x'], p['y']) != (after_loss[0]['x'], after_loss[0]['y']) for p in after_loss):
        violations.append('cancelled follower moved during hidden/visible recovery phases')
    late_updates = [r for r in group_destinations if r['tick_before'] >= 80]
    if late_updates:
        violations.append('cancelled follow group acquired a later destination')
    before_shift = [r for r in rows if r.get('event') == 'marker' and
                    ' label=before_hidden_shift ' in r.get('value', '')]
    if len(before_shift) != 1 or not before_shift[0]['value'].endswith(' order=0'):
        violations.append('cancellation was not observed before the controlled target shift')
    if not transitions or any(r.get('blocked') != 0 for r in transitions):
        violations.append('visible control lacks successful group visibility callbacks')
    return {'samples': len(positions), 'visibility_transitions': transitions,
            'blocked_group_callbacks': len(blocked),
            'group_destination_updates_after_loss': len(late_updates),
            'outcome': 'smart_follow_cancelled' if not violations else 'unverified',
            'final': positions[-1] if positions else None}


def check_fog_experiment(rows, violations, reacquire):
    shift_tick, restore_tick = (70, 75) if reacquire else (80, 160)
    positions, followers, transitions, changes = [], [], [], []
    targets, tick = {}, 0
    for r in rows:
        event = r.get('event')
        if event == 'target-marker':
            m = re.fullmatch(r'PATHTARGET tick=(\d+) x=(-?[\d.]+) y=(-?[\d.]+) visible=([01])', r.get('value', ''))
            if not m:
                violations.append('malformed fog-target sample')
                continue
            tick = int(m[1])
            positions.append(dict(tick=tick, x=float(m[2]), y=float(m[3]), visible=int(m[4])))
        elif event == 'marker':
            m = re.fullmatch(r'PATHTRACE tick=(\d+) label=sample x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
            if m:
                followers.append(dict(tick=int(m[1]), x=float(m[2]), y=float(m[3]), order=int(m[4])))
        elif event == 'group-target':
            targets[r.get('path')] = r.get('target')
        elif event == 'target-visibility' and targets.get(r.get('path')) == r.get('target'):
            transitions.append({**r, 'tick_before': tick})
        elif event == 'path-destination' and r.get('caller') == '0x16ce73' and targets.get(r.get('path')) not in (None, '0x0'):
            changes.append({**r, 'tick_before': tick})
    if [p['tick'] for p in positions] != list(range(1, 301)):
        violations.append('fog target lacks 300 ordered samples')
    if any(p['x'] != -1936 or p['y'] != (-144 if p['tick'] < shift_tick else 112) for p in positions):
        violations.append('fog target jump differs from scenario')
    if any(p['visible'] != (0 if 62 <= p['tick'] < restore_tick else 1) for p in positions
           if p['tick'] < 60 or 62 <= p['tick'] < restore_tick or p['tick'] >= restore_tick + 2):
        violations.append('fog did not produce required visibility phases')
    blocked = [t for t in transitions if t.get('blocked') == 1 and 59 <= t['tick_before'] <= 62]
    if not blocked:
        violations.append('fog lacks linked blocking group callback')
    hidden_changes = [c for c in changes if 62 <= c['tick_before'] < restore_tick - 1]
    if hidden_changes:
        violations.append('group destination changed during controlled fog loss')
    narrow = [r for r in rows if r.get('event') == 'arrival-transition' and abs(r.get('threshold', 0) - 0.49) < 1e-6]
    if not narrow:
        violations.append('fog lacks temporary narrow arrival range')
    if any(r.get('event') == 'target-lost-dispatch' for r in rows):
        violations.append('fog fixture unexpectedly dispatched a target-lost notification')
    restored = []
    if reacquire:
        restored = [t for t in transitions if t.get('blocked') == 0 and restore_tick - 1 <= t['tick_before'] <= restore_tick + 2
                    and any((t.get('group'), t.get('path'), t.get('target')) == (b.get('group'), b.get('path'), b.get('target')) for b in blocked)]
        if not restored:
            violations.append('fog lacks visibility reacquisition by the same group')
        gates = [r for r in rows if r.get('event') == 'group-completion' and
                 any(r.get('group') == b.get('group') for b in blocked)]
        if not any(r.get('missed') == 33 and r.get('gateOpen') is True and r.get('flags', 0) & 1 for r in gates):
            violations.append('fog lacks the 33-missed-sample completion gate witness')
        if not any(r.get('missed') == 0 and r.get('gateOpen') is False and
                   any(r.get('counter', -1) >= t.get('counter', 0) for t in restored) for r in gates):
            violations.append('fog completion gate did not close again after reacquisition')
        if not any(c.get('destination') == [163.5, 99.5] and restore_tick - 1 <= c['tick_before'] < restore_tick + 20 for c in changes):
            violations.append('fog reacquisition lacks the new target destination')
        if any(p['order'] != 851971 for p in followers if p['tick'] >= 10):
            violations.append('follow order did not persist across fog reacquisition')
        if not any(r.get('event') == 'arrival-transition' and r.get('threshold') == 11.3125 and
                   r.get('previous', {}) and abs(r['previous'].get('threshold', 0) - 0.49) < 1e-6 for r in rows):
            violations.append('arrival range did not restore after fog reacquisition')
    else:
        if any(p['order'] != 0 for p in followers if p['tick'] >= 80):
            violations.append('long fog follow did not remain stopped')
        if not any(r.get('result') == 1 and r.get('flags', 0) & 0x10000 for r in narrow):
            violations.append('long fog lacks forced arrival witness')
    return {'samples': len(positions), 'blocked_callbacks': blocked, 'reacquired_callbacks': restored,
            'hidden_group_destination_updates': len(hidden_changes), 'narrow_arrivals': len(narrow),
            'final': positions[-1] if positions else None}


def target_loss_chains(rows):
    """Count linked notification -> Move rejection -> observed cancellation stacks."""
    chains = []
    for i, dispatch in enumerate(rows):
        if dispatch.get('event') != 'target-lost-dispatch' or dispatch.get('caller') != '0x68b7d8':
            continue
        # These are synchronous callbacks; unrelated later events cannot satisfy
        # the chain. Use their timestamp as well as target and ability identity.
        same_tick = [r for r in rows[i + 1:] if r.get('ms') == dispatch.get('ms')]
        handlers = [r for r in same_tick if r.get('event') == 'move-target-lost' and
                    r.get('targetUnit') == dispatch.get('targetUnit') and r.get('eventCode') == 0xd01a4]
        for handler in handlers:
            start = same_tick.index(handler)
            checks = [r for r in same_tick[start + 1:] if r.get('event') == 'move-target-validation' and
                      r.get('ability') == handler.get('ability') and r.get('target') == dispatch.get('targetUnit') and r.get('result') == 0xdd]
            if not checks:
                continue
            stops = [r for r in same_tick[same_tick.index(checks[0]) + 1:] if r.get('event') == 'mover-stop' and
                     r.get('caller') == '0x5ca8b' and '0x5ff645' in r.get('stack', []) and '0x65108f' in r.get('stack', [])]
            if stops:
                chains.append({'targetUnit': dispatch['targetUnit'], 'ownerUnit': handler.get('ownerUnit'),
                               'ability': handler['ability'], 'result': 0xdd, 'stops': len(stops)})
    return chains


def check_order_lifecycle(markers, violations):
    """Public command ownership, deliberately independent of trajectory equality."""
    expected = [(0, 'start_order_lifecycle', 0), (10, 'point_move_accepted', 851986),
                (30, 'hold_accepted', 0), (60, 'defend_accepted', 0),
                (65, 'undefend_accepted', 0), (80, 'target_smart_accepted', 851971),
                (110, 'target_near', 851971), (120, 'target_move_accepted', 851986),
                (130, 'target_removed', 0), (140, 'stop_accepted', 0),
                (150, 'patrol_accepted', 851991), (170, 'invalid_rejected', 851991),
                (180, 'hold_again_accepted', 0), (200, 'enemy_near', 0),
                (230, 'follow_before_death_accepted', 851986), (240, 'killed', 0),
                (250, 'dead_move_rejected', 0), (300, 'complete', 0)]
    transitions = [(m['tick'], m['label'], m['order']) for m in markers if m['label'] != 'sample']
    if transitions != expected:
        violations.append('public order lifecycle transitions differ from fixture')
    phases = [(0, 10, 0), (10, 30, 851986), (30, 80, 0), (80, 120, 851971),
              (120, 130, 851986), (130, 150, 0), (150, 180, 851991),
              (180, 230, 0), (230, 240, 851986), (240, 301, 0)]
    samples = [m for m in markers if m['label'] == 'sample']
    if any(m['order'] != order for begin, end, order in phases for m in samples if begin <= m['tick'] < end):
        violations.append('public order lifecycle sampled head differs from fixture')
    return transitions


def check_patrol_reversal(markers, violations):
    """The authored Patrol must approach (-1936,-144), then return while head851991 remains active."""
    samples = [m for m in markers if m['label'] == 'sample' and 150 <= m['tick'] < 180]
    if (len(samples) == 30 and [m['tick'] for m in samples] == list(range(150, 180)) and
        all(m['order'] == 851991 and math.isfinite(m.get('x', math.nan)) and
            math.isfinite(m.get('y', math.nan)) for m in samples)):
        distances = [math.hypot(m['x'] + 1936, m['y'] + 144) for m in samples]
        closest = min(distances)
        if closest < 64 and distances[0] > closest + 128 and distances[-1] > closest + 64:
            return
    violations.append('Patrol lacks endpoint approach and return under its current head')


def analyze(rows, scenario=None):
    violations, summaries = [], {}
    metadata = [r for r in rows if r.get('event') == 'metadata']
    endings = [r for r in rows if r.get('event') == 'trace-end']
    searches = [r for r in rows if r.get('event') == 'search']
    if len(metadata) != 1 or metadata[0].get('sha256') != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        violations.append('missing/unsupported binary identity')
    if len(endings) != 1 or not endings[0].get('installed'):
        violations.append('missing successful observer completion')
    end = endings[-1] if endings else {}
    if end.get('samples') != len(searches):
        violations.append('sample count differs from emitted searches')
    if any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        violations.append('capture contains observer/controller errors')
    counts = end.get('counts', {})
    for kind in ('fine', 'acc'):
        samples = [r for r in searches if r.get('kind') == kind]
        total = counts.get(kind + '-search', 0)
        if total < len(samples):
            violations.append(kind + ': counters smaller than emitted sample count')
        levels = collections.Counter()
        for row in samples:
            if row['nodes'] < 0 or not 0 <= row['pops'] <= row['budget'] + 1:
                violations.append(kind + ': invalid node/pop/budget fields')
            if kind == 'acc':
                hist = row.get('levels', {})
                if set(hist) - {'0', '1', '2', '3'} or sum(hist.values()) != row['nodes']:
                    violations.append('acc: invalid level histogram')
                levels.update(hist)
            elif row.get('footprintClass') not in range(4):
                violations.append('fine: invalid footprint class')
        summaries[kind] = {'calls': total, 'sampled': len(samples),
                           'sampled_paths': len({r['path'] for r in samples if 'path' in r}),
                           'sampled_budgets': sorted({r['budget'] for r in samples}),
                           'sampled_negative_results': sum(r['result'] < 0 for r in samples),
                           'sampled_budget_stops': sum(r['result'] < 0 and r['pops'] > r['budget'] for r in samples)}
        if kind == 'acc':
            summaries[kind]['node_levels_summed_over_samples'] = dict(levels)
    if any(r.get('kind') not in ('fine', 'acc') for r in searches):
        violations.append('unknown search kind')
    refreshes = [r for r in rows if r.get('event') == 'target-refresh']
    refresh_gaps, last_refresh = collections.Counter(), {}
    for row in rows:
        if row.get('event') == 'group-target':
            last_refresh.pop((row.get('group'), row.get('path')), None)
        if row.get('event') != 'target-refresh':
            continue
        expected = max(16, min(132, row['unclamped'])) + (165 if row['flags'] & 0x400 else 0)
        if row['reload'] != expected:
            violations.append('target refresh reload contradicts clamp/flag policy')
        if not math.isfinite(row['distanceFine']) or not math.isfinite(row['coefficient']):
            violations.append('target refresh has invalid numeric inputs')
        key = (row.get('group'), row.get('path'))
        if key in last_refresh:
            refresh_gaps[(row['counter'] - last_refresh[key]) & 0xffffffff] += 1
        last_refresh[key] = row['counter']
    completions = [r for r in rows if r.get('event') == 'group-completion']
    for row in completions:
        if row.get('gateOpen') != (not row.get('flags', 0) & 1 or row.get('missed', 0) >= 33):
            violations.append('group completion contradicts unseen-count gate')
    destinations = [r for r in rows if r.get('event') == 'path-destination']
    for row in destinations:
        if row.get('counts') != [0, 0] or row.get('indices') != [-1, -1]:
            violations.append('destination setter failed to clear both route buffers/indices')
        if row.get('afterFlags', 0) & 0x30100000:
            violations.append('destination setter retained disabled/result flags')
        if row.get('replaceOriginal') and row.get('original') != row.get('destination'):
            violations.append('destination setter failed to replace original coordinates')
    stopped_resets = [r for r in destinations if r.get('caller') == '0x171415' and
                      r.get('destination') == [-128000, -128000] and r.get('replaceOriginal') == 0 and
                      r.get('original') != r.get('destination')]
    replans = [r for r in rows if r.get('event') == 'replan-check']
    for row in replans:
        old, new, shift = row['oldDestination'], row['destination'], row['shift'] & 31
        changed = any((math.floor(a) >> shift) != (math.floor(b) >> shift) for a, b in zip(old, new))
        ready = not changed or all(((row['counter'] - stamp) & 0xffffffff) >= 10 for stamp in row['timestamps'])
        if row['changed'] != int(changed) or row['ready'] != int(ready):
            violations.append('replan observation contradicts cell/timestamp gate')
    retry_initializations, retry_results = {}, []
    for row in rows:
        if row.get('event') == 'retry-init':
            if row.get('count') not in (2, 7, 8) or row.get('thresholdSquared') != 144:
                violations.append('retry initializer has unexpected count or threshold')
            retry_initializations[row.get('path')] = row
        elif row.get('event') == 'retry-result':
            retry_results.append(row)
            initial = row.get('before')
            result = row.get('result')
            if result == 3:
                expected = initial
            elif result == 4:
                expected = 1
                if initial != 1:
                    violations.append('retry exhaustion did not enter with count one')
            elif result == 1:
                count = initial or retry_initializations.get(row.get('path'), {}).get('count', 0)
                expected = (count - 1) & 0xffffffff
                if count <= 1:
                    violations.append('retry result lacks a positive initialized countdown')
            else:
                expected = None
                violations.append('unknown target-or-retry result')
            if row.get('after') != expected:
                violations.append('retry count transition contradicts original routine')
    exhausted = [r for r in retry_results if r.get('result') == 4]
    target_hits = [r for r in rows if r.get('event') == 'target-perimeter-hit']
    for hit in target_hits:
        if (hit.get('offset'), hit.get('width')) not in ((1, 3), (2, 4), (2, 5), (3, 6)) or not hit.get('matched'):
            violations.append('target perimeter hit lacks supported footprint or matched cell')
            continue
        x, y = hit['matched']
        lo_x, lo_y = (v - hit['offset'] for v in hit['center'])
        hi_x, hi_y = lo_x + hit['width'] - 1, lo_y + hit['width'] - 1
        if not (lo_x <= x <= hi_x and lo_y <= y <= hi_y and (x in (lo_x, hi_x) or y in (lo_y, hi_y))):
            violations.append('target hit lies outside the scanned perimeter')
    forces = [r for r in rows if r.get('event') == 'force-arrival']
    for row in forces:
        if row.get('after') != row.get('before', 0) | 0x10000:
            violations.append('forced arrival setter changed unexpected flags')
        if row.get('caller') == '0x16fd32':
            advance = row.get('advance') or {}
            if (advance.get('result') not in (3, 4) or advance.get('path') != row.get('path') or
                    advance.get('counter') != row.get('counter')):
                violations.append('forced arrival lacks same-path current-update route result 3/4')
            if target_hits and advance.get('result') == 3 and not any(
                    h.get('target') == advance.get('target') and h.get('counter') == row.get('counter') for h in target_hits):
                violations.append('route-result-3 force lacks matching target perimeter witness')
    arrivals = [r for r in rows if r.get('event') == 'arrival-transition']
    for arrival in arrivals:
        values = [arrival.get('threshold'), arrival.get('angle'), arrival.get('footprint')]
        if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in values):
            violations.append('arrival transition contains invalid scalar values')
            continue
        if arrival.get('result') not in (0, 1) or arrival.get('inRange') not in (0, 1):
            violations.append('arrival transition contains invalid boolean results')
        elif arrival['result'] != int(arrival['inRange'] == 1 and abs(arrival['angle']) <= 0.20000000298023224):
            violations.append('arrival result contradicts range/heading predicate')
    routes = [r for r in rows if r.get('event') == 'route']
    for route in routes:
        points = route.get('points', [])
        count = route.get('count', -1)
        if not 0 <= count <= 65536 or len(points) != min(count, 256) or route.get('truncated') != (count > len(points)):
            violations.append('route: inconsistent count or truncation')
        if any(not isinstance(p, list) or len(p) != 2 or
               any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in p) for p in points):
            violations.append('route: invalid coordinate pair')
    snapshots = [r for r in rows if r.get('event') == 'cell-snapshot']
    traversals = [r for r in rows if r.get('event') == 'gate-traversal']
    if len(traversals) != counts.get('gate-traversal', 0):
        violations.append('gate traversal completion count differs from entry count')
    for snapshot in snapshots:
        cells = snapshot.get('cells', [])
        if [c.get('level') for c in cells] != [-1, 0, 1, 2, 3]:
            violations.append('cell snapshot: incomplete hierarchy')
            continue
        for cell in cells:
            level, word = cell['level'], cell.get('word', -1)
            if not isinstance(word, int) or not 0 <= word <= 0xffffffff:
                violations.append('cell snapshot: invalid word')
                continue
            if level >= 0 and cell.get('classes') != [(word >> (30 - shift)) & 3 for shift in (0, 2, 4, 6)]:
                violations.append('cell snapshot: classes differ from stored word')
            if any(cell.get(axis) != cells[0].get(axis, -1) >> (level + 1) for axis in ('x', 'y')):
                violations.append('cell snapshot: wrong parent coordinates')
    exhausted_forces = [f for f in forces if f.get('caller') == '0x16fd32' and
                        (f.get('advance') or {}).get('result') == 4 and
                        any(r.get('path') == f.get('path') and r.get('counter') == f.get('counter') for r in exhausted)]
    probe = None
    gate_change = None
    target_motion = None
    crowd = None
    if scenario is not None:
        markers = []
        for row in rows:
            if row.get('event') != 'marker':
                continue
            match = re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', row.get('value', ''))
            if match:
                tick, label, x, y, order = match.groups()
                markers.append(dict(tick=int(tick), label=label, x=float(x), y=float(y), order=int(order)))
        labels = [m['label'] for m in markers]
        admission_label = 'point_move_accepted' if scenario == 'order_lifecycle' else 'order_accepted'
        if labels.count('start_' + scenario) != 1 or labels.count('complete') != 1 or labels.count(admission_label) != 1:
            violations.append('scenario lacks unique start, accepted order, or completion')
        samples = [m for m in markers if m['label'] == 'sample']
        if [m['tick'] for m in samples] != list(range(1, 301)):
            violations.append('scenario sample ticks are incomplete or out of order')
        if any(label in labels for label in ('order_rejected', 'stop_rejected', 'reorder_rejected')):
            violations.append('scenario move order rejected')
        expected = {'open': {}, 'turn': {}, 'stock_turn': {}, 'wall': {'wall_query_true': 1},
                    'insert': {'before_insert': 1, 'after_insert': 1, 'wall_query_true': 1},
                    'remove': {'before_remove': 1, 'after_remove': 1, 'wall_query_true': 1, 'wall_query_false': 1}}
        expected['pathing_toggle'] = {}
        expected['order_lifecycle'] = {}
        expected['remove_reorder'] = {**expected['remove'], 'before_reorder': 1, 'stop_accepted': 1, 'reorder_accepted': 1}
        expected['crowd'] = expected['crowd_air'] = {}
        expected['blocked_goal'] = {'goal_blocked': 1}
        expected['widget_lifecycle'] = {label: 1 for label in ('before_widget_create', 'after_widget_create', 'before_widget_remove', 'after_widget_remove')}
        expected['follow'] = {}
        expected['follow_invisible'] = {label: 1 for label in (
            'before_invisibility', 'invisibility_added', 'before_hidden_shift',
            'after_hidden_shift', 'before_visibility_restore', 'invisibility_removed')}
        expected['follow_fog'] = expected['follow_fog_reacquire'] = {label: 1 for label in (
            'before_fog', 'fog_started', 'before_hidden_shift', 'after_hidden_shift',
            'before_visibility_restore', 'fog_removed')}
        expected['follow_walk'] = {'before_target_move': 1, 'target_move_accepted': 1}
        expected['follow_shift'] = {'before_target_shift': 1, 'after_target_shift': 1}
        expected['owner_change'] = {'before_owner_change': 1, 'after_owner_change': 1}
        expected['gate'] = {'gate_active': 1}
        expected['gate_off'] = {'gate_inactive': 1}
        expected['gate_retarget'] = {'gate_active': 1, 'before_gate_retarget': 1, 'after_gate_retarget': 1}
        expected['gate_disable'] = {'gate_active': 1, 'before_gate_disable': 1, 'after_gate_disable': 1}
        if any(labels.count(label) != count for label, count in expected[scenario].items()):
            violations.append('scenario obstacle transition markers missing or duplicated')
        if scenario == 'pathing_toggle':
            check_pathing_toggle(rows, markers, violations)
        if scenario == 'order_lifecycle':
            check_order_lifecycle(markers, violations)
            check_patrol_reversal(markers, violations)
            hold = [r.get('value', '') for r in rows if r.get('event') == 'hold-marker']
            damage = [re.fullmatch(r'PATHHOLD tick=(200|220) life=(-?[\d.]+)', value) for value in hold]
            if (len(damage) != 2 or not all(damage) or
                [int(m[1]) for m in damage] != [200, 220] or
                not 0 < float(damage[1][2]) < float(damage[0][2])):
                violations.append('Hold lacks observed automatic damage while current head is retired')
        if scenario == 'stock_turn':
            stock = [r.get('value') for r in rows if r.get('event') == 'stock-marker']
            if stock != ['PATHSTOCK tick=0 turn=0.600 window=1.047 defaultTurn=0.600 defaultWindow=60.000',
                         'PATHSTOCK tick=200 turn=0.125 window=0.500 defaultTurn=0.600 defaultWindow=60.000']:
                violations.append('stock getters or default preservation differ from authored Footman values')
        if scenario in ('follow', 'follow_shift', 'follow_walk', 'follow_invisible', 'follow_fog', 'follow_fog_reacquire'):
            targets = [r for r in rows if r.get('event') == 'group-target' and r.get('target') not in (None, '0x0')]
            target_paths = {r.get('path') for r in targets}
            # Group paths are pooled and addresses can be reused by point orders.
            # Bind each search to the most recent target assignment, not any
            # historical assignment of that address.
            current_targets, linked_group_searches = {}, []
            for r in rows:
                if r.get('event') == 'group-target':
                    current_targets[r.get('path')] = r.get('target')
                elif r.get('event') == 'search' and current_targets.get(r.get('path')) not in (None, '0x0'):
                    linked_group_searches.append(r)
            switches = [r for r in rows if r.get('event') == 'scheduler-target' and r.get('path') in target_paths
                        and r.get('value') == 1 and r.get('after', 0) & 0x4000000 and r.get('accLimit') == 5000]
            admissions = [r for r in rows if r.get('event') == 'scheduler-admission' and r.get('path') in target_paths
                          and r.get('bucketOffset', -1) % 0x70 == 0x1c and r.get('result') == 1]
            admitted_searches = [r for r in linked_group_searches if r.get('kind') == 'acc' and r.get('budget') == 5000
                                 and any(r.get('path') == a.get('path') and r.get('request') == a.get('request')
                                         for a in admissions)]
            if not targets or not switches or not admissions or not admitted_searches:
                violations.append('follow lacks target-linked group path, flag, 5000 request limit, or target-bucket admission')
        if scenario in ('follow_fog', 'follow_fog_reacquire'):
            target_motion = check_fog_experiment(rows, violations, scenario == 'follow_fog_reacquire')
        if scenario == 'follow_invisible':
            target_motion = check_visibility_experiment(rows, violations)
        if scenario == 'follow_walk':
            positions = []
            for r in rows:
                if r.get('event') != 'target-marker':
                    continue
                match = re.fullmatch(r'PATHTARGET tick=(\d+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
                if match:
                    positions.append(dict(tick=int(match[1]), x=float(match[2]), y=float(match[3]), order=int(match[4])))
                else:
                    violations.append('malformed moving-target position sample')
            if [p['tick'] for p in positions] != list(range(1, 301)):
                violations.append('moving target lacks 300 ordered samples')
            if positions:
                steps = [math.hypot(b['x'] - a['x'], b['y'] - a['y']) for a, b in zip(positions, positions[1:])]
                if any(p['x'] != -1936 or p['y'] != -144 for p in positions if p['tick'] < 80):
                    violations.append('target moved before its controlled order')
                if not steps or not 0 < max(steps) < 32:
                    violations.append('target lacks incremental movement or moved by a jump')
                if math.hypot(positions[-1]['x'] + 1936, positions[-1]['y'] - 112) > 32:
                    violations.append('moving target did not reach the test destination vicinity')
                target_motion = {'samples': len(positions), 'max_step_world': max(steps) if steps else 0,
                                 'final': positions[-1]}
            after = [i for i, r in enumerate(rows) if r.get('event') == 'marker' and ' label=target_move_accepted ' in r.get('value', '')]
            if len(after) == 1:
                later_group = [r for r in rows[after[0]+1:] if r.get('event') == 'search' and r in linked_group_searches]
                if not later_group:
                    violations.append('moving target lacks a later follower-group search')
                if target_motion is not None:
                    target_motion['follower_group_searches_after_move'] = len(later_group)
        if scenario == 'follow_shift':
            target_markers = [r.get('value') for r in rows if r.get('event') == 'target-marker']
            if target_markers != ['PATHTARGET tick=80 x=-1936.000 y=112.000']:
                violations.append('follow shift target position is not the requested controlled change')
            after = [i for i, r in enumerate(rows) if r.get('event') == 'marker' and ' label=after_target_shift ' in r.get('value', '')]
            if len(after) == 1:
                later = rows[after[0]+1:]
                if not any(r.get('event') == 'search' and r.get('kind') == 'acc' for r in later):
                    violations.append('follow shift lacks a later accelerated search')
                if not any(r.get('event') == 'replan-check' and r.get('changed') == r.get('ready') == 1 for r in later):
                    violations.append('follow shift lacks a ready destination-cell change')
        if scenario == 'owner_change':
            before = [i for i, r in enumerate(rows) if r.get('event') == 'marker' and ' label=before_owner_change ' in r.get('value', '')]
            after = [i for i, r in enumerate(rows) if r.get('event') == 'marker' and ' label=after_owner_change ' in r.get('value', '')]
            if len(before) == len(after) == 1:
                changes = [r for r in rows[before[0]+1:after[0]] if r.get('event') == 'scheduler-class']
                if before[0] >= after[0] or len(changes) != 1 or not (
                        changes[0].get('value') == 1 and
                        ((changes[0].get('before', -1) >> 16) & 15) == 0 and
                        ((changes[0].get('after', -1) >> 16) & 15) == 1):
                    violations.append('owner change lacks synchronous scheduler row 0-to-1 transition')
        if scenario in ('gate_retarget', 'gate_disable'):
            operation = scenario.removeprefix('gate_')
            def positions(label):
                return [i for i, r in enumerate(rows) if r.get('event') == 'marker' and
                        (' label=' + label + ' ') in r.get('value', '')]
            before, after = positions('before_gate_' + operation), positions('after_gate_' + operation)
            if len(before) == len(after) == 1:
                cached = [r for r in rows[:before[0]] if r.get('event') == 'route' and
                          r.get('kind') == 'acc' and any(isinstance(p, list) and len(p) == 2 and
                          p[0] == -128000.0078125 for p in r.get('points', []))]
                early = sum(r.get('event') == 'gate-traversal' for r in rows[:after[0]])
                if before[0] >= after[0] or not cached or early:
                    violations.append('gate edit lacks cached special-edge route before edit and traversal after edit')
                gate_change = {'cached_special_routes': len(cached), 'traversals_before_edit_completed': early,
                               'traversals_after_edit': sum(r.get('event') == 'gate-traversal' for r in rows[after[0] + 1:])}
        if scenario in ('crowd', 'crowd_air'):
            crowd = check_crowd_experiment(rows, violations)
            if scenario == 'crowd':
                crowd['arrivals'] = check_ground_crowd_arrivals(rows, crowd, violations)
        if scenario == 'blocked_goal':
            expected_cells = [(-2000 + x * 32, -208 + y * 32) for x in range(5) for y in range(5)]
            actual_cells = [(r.get('x'), r.get('y')) for r in rows if r.get('event') == 'terrain-native']
            if actual_cells != expected_cells:
                violations.append('blocked goal terrain coordinates differ from fixture')
            if not exhausted_forces:
                violations.append('blocked goal lacks retry exhaustion linked to forced arrival')
            if not any(a.get('result') == 1 and a.get('flags', 0) & 0x10000 and
                       any(a.get('mover') == f.get('mover') for f in exhausted_forces) for a in arrivals):
                violations.append('blocked goal lacks accepted forced arrival for exhausted mover')
            if samples and samples[-1]['order'] != 0:
                violations.append('blocked goal order remained active at completion')
        terrain = [r for r in rows if r.get('event') == 'terrain-native']
        states = {'open': [], 'turn': [], 'stock_turn': [], 'wall': [0] * 4, 'insert': [0] * 4, 'remove': [0] * 4 + [1] * 4}
        states['pathing_toggle'] = [0]*4
        states['remove_reorder'] = states['remove']
        states['follow'] = states['follow_shift'] = states['follow_walk'] = states['follow_invisible'] = states['follow_fog'] = states['follow_fog_reacquire'] = []
        states['crowd'] = states['crowd_air'] = []
        states['widget_lifecycle'] = states['order_lifecycle'] = []
        states['blocked_goal'] = [0] * 25
        states['owner_change'] = []
        states['gate'] = states['gate_off'] = []
        states['gate_retarget'] = states['gate_disable'] = []
        if [r.get('passable') for r in terrain] != states[scenario]:
            violations.append('scenario terrain edits differ from expected calls')
        probe = {'scenario': scenario, 'markers': len(markers), 'labels': dict(collections.Counter(labels)),
                 'final': markers[-1] if markers else None,
                 'x_range': [min(m['x'] for m in samples), max(m['x'] for m in samples)] if samples else None}
    separation = [r for r in rows if r.get('event') == 'separation-active']
    for row in separation:
        vectors = [row.get(name, []) for name in ('vector', 'afterVector', 'position', 'afterPosition')]
        if any(len(v) != 2 or not all(isinstance(n, (float, int)) and math.isfinite(n) for n in v) for v in vectors):
            violations.append('separation contains invalid vector/position')
            continue
        selector = row.get('selector')
        if selector in range(5):
            cap = 0.5 if selector == 0 else 0.2
            if math.hypot(*row['afterVector']) > cap + 0.0001:
                violations.append('separation exceeds authored configuration cap')
    yielding = summarize_yielding(rows, violations)
    blocker_summary = summarize_blockers(rows, violations)
    return {'widgets': summarize_widgets(rows, violations), 'tasks': summarize_tasks(rows, violations), 'yielding': yielding, 'blockers': blocker_summary, 'crowd': crowd, 'separation_active': len(separation), 'separation_movers': len({r['mover'] for r in separation}), 'retry_results': len(retry_results), 'retry_exhaustions': len(exhausted), 'exhaustion_forced_arrivals': len(exhausted_forces), 'target_perimeter_hits': len(target_hits), 'forced_arrivals': len(forces), 'group_completion_samples': len(completions), 'target_loss_chains': target_loss_chains(rows), 'target_refresh': {'samples': len(refreshes),
                              'coefficients': sorted({r['coefficient'] for r in refreshes}),
                              'reloads': sorted({r['reload'] for r in refreshes}),
                              'counter_gap_histogram': dict(refresh_gaps)},
            'stopped_path_resets': len(stopped_resets), 'replan_checks': len(replans), 'searches': summaries, 'route_samples': len(routes), 'cell_snapshots': len(snapshots),
            'gate_traversals': {'calls': len(traversals), 'successful': sum(r.get('result') == 1 for r in traversals)},
            'arrival': {'transitions': len(arrivals), 'arrived': sum(r.get('result') == 1 for r in arrivals),
                        'thresholds': sorted({r['threshold'] for r in arrivals if isinstance(r.get('threshold'), (int, float))})},
            'target_motion': target_motion, 'gate_change': gate_change,
            'hierarchy_updates': counts.get('hierarchy-update', 0), 'probe': probe,
            'map_snapshots': [r for r in rows if r.get('event') == 'maps'], 'violations': violations}


def compare_order_lifecycle(rows, repeat, violations):
    violations.extend('repeat: ' + error for error in analyze(repeat, 'order_lifecycle')['violations'])
    metadata = [[{k: v for k, v in row.items() if k != 'pid'} for row in capture if row.get('event') == 'metadata']
                for capture in (rows, repeat)]
    values = [[row['value'] for row in capture if row.get('event') in ('marker', 'hold-marker')]
              for capture in (rows, repeat)]
    equal = metadata[0] == metadata[1] and values[0] == values[1]
    if not equal:
        violations.append('order lifecycle repeat metadata or timer markers differ')
    return dict(equal=equal, markers=len(values[0]),
                digests=[hashlib.sha256(json.dumps(v, separators=(',', ':')).encode()).hexdigest() for v in values])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compare', type=Path, help='require identical complete public order-lifecycle timer/health markers')
    parser.add_argument('--require', choices=['widget', 'task', 'fine', 'acc', 'hierarchy', 'gate', 'arrival', 'reset', 'refresh', 'target-loss', 'target-perimeter', 'retry-exhaustion', 'separation'], action='append', default=[])
    parser.add_argument('--scenario', choices=['pathing_toggle', 'open', 'turn', 'stock_turn', 'wall', 'insert', 'remove', 'remove_reorder', 'gate', 'gate_off', 'gate_retarget', 'gate_disable', 'owner_change', 'follow', 'follow_shift', 'follow_walk', 'follow_invisible', 'follow_fog', 'follow_fog_reacquire', 'blocked_goal', 'crowd', 'crowd_air', 'widget_lifecycle', 'order_lifecycle'])
    args = parser.parse_args()
    if args.compare and args.scenario != 'order_lifecycle':
        parser.error('--compare requires --scenario order_lifecycle')
    rows = [json.loads(line) for line in args.trace.read_text().splitlines() if line.strip()]
    result = analyze(rows, args.scenario)
    if args.compare:
        repeat = [json.loads(line) for line in args.compare.read_text().splitlines() if line.strip()]
        result['repeat'] = compare_order_lifecycle(rows, repeat, result['violations'])
    for kind in args.require:
        present = (len(result['widgets']['paired_create_destroy']) if kind == 'widget' else result['tasks']['accepted'] if kind == 'task' else result['separation_active'] if kind == 'separation' else result['retry_exhaustions'] if kind == 'retry-exhaustion' else result['target_perimeter_hits'] if kind == 'target-perimeter' else len(result['target_loss_chains']) if kind == 'target-loss' else result['target_refresh']['samples'] if kind == 'refresh' else result['stopped_path_resets'] if kind == 'reset' else result['arrival']['arrived'] if kind == 'arrival' else result['gate_traversals']['successful'] if kind == 'gate' else
                   result['hierarchy_updates'] if kind == 'hierarchy' else result['searches'][kind]['sampled'])
        if not present:
            result['violations'].append('missing required witness: ' + kind)
    body = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(body)
    print(body, end='')
    return bool(result['violations'])


if __name__ == '__main__':
    raise SystemExit(main())
