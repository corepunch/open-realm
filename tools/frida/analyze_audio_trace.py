#!/usr/bin/env python3
"""Check observed retail audio contracts; missing witnesses fail --require-complete."""
import argparse
import collections
import hashlib
import json
import subprocess
from pathlib import Path


def analyze(events):
    stacks, responses = collections.defaultdict(list), collections.defaultdict(list)
    calls, completed, starts, ends, cooldowns = [], [], [], [], []
    violations = []
    for e in events:
        kind, thread = e.get('event'), e.get('thread')
        if e.get('type') == 'error':
            violations.append({'error': 'Frida script error', 'detail': e})
        if kind == 'label-request':
            call = {'request': e, 'before': [], 'after': [], 'admissions': []}
            stacks[thread].append(call)
            if responses[thread]: responses[thread][-1]['labels'].append(call)
        elif kind == 'label-result':
            if not stacks[thread] or stacks[thread][-1]['request']['id'] != e['id']:
                violations.append({'error': 'unmatched label result', 'seq': e['seq']})
                continue
            call = stacks[thread].pop(); call['result'] = e; calls.append(call)
        elif kind in ('variant-before', 'variant-after', 'admit-result') and stacks[thread]:
            stacks[thread][-1][{'variant-before': 'before', 'variant-after': 'after', 'admit-result': 'admissions'}[kind]].append(e)
        elif kind in ('what', 'pissed', 'yes', 'yes-attack'):
            responses[thread].append({'request': e, 'labels': []})
        elif kind in ('what-done', 'pissed-done', 'yes-done', 'yes-attack-done'):
            if responses[thread]:
                response = responses[thread].pop(); response['result'] = e; completed.append(response)
        elif kind == 'portrait-start': starts.append(e)
        elif kind == 'portrait-end': ends.append(e)
        elif kind == 'cooldown-set': cooldowns.append(e)
    rollback = []
    for c in calls:
        rejected = [a for a in c['admissions'] if a['result'] != 0]
        if not rejected or not c['before']: continue
        before, after = c['before'][-1]['row'], c['result']['row']
        if not after or 'variant' not in before: continue
        witness = {'label': c['request']['label'], 'seq': c['request']['seq'],
                   'before': before['variant'], 'attempted': c['after'][-1]['row']['variant'],
                   'restored': after['variant'], 'backend_result': rejected[-1]['result'],
                   'label_result': c['result']['result']}
        rollback.append(witness)
        if before['variant'] != after['variant']: violations.append({'error': 'variant not restored', **witness})
    rejected_counts = []
    for r in completed:
        if r['request']['event'] != 'what': continue
        if not any(c.get('result', {}).get('result') == 2 for c in r['labels']): continue
        witness = {'seq': r['request']['seq'], 'before': r['request']['count'], 'after': r['result']['count']}
        rejected_counts.append(witness)
        if witness['before'] != witness['after']: violations.append({'error': 'rejected What advanced count', **witness})
    files = collections.defaultdict(list)
    for c in calls:
        for a in c['admissions']:
            if a['result'] == 0:
                sound = a['sound']
                files[sound['file'].lower()].append({'label': c['request']['label'], 'channel': sound['channel'],
                                                    'priority': sound['priority'], 'flags': sound['flags'], 'seq': a['seq']})
    aliases = [{'file': f, 'requests': rows} for f, rows in files.items()
               if len({r['label'] for r in rows}) > 1 and len({(r['channel'], r['priority'], r['flags']) for r in rows}) > 1]
    lifecycle = []
    for start in starts:
        unit = start['notification']['unit']
        callback = start.get('callback')
        if not callback: continue # old traces cannot disambiguate queued requests for one unit
        instance = callback['sound']['address']
        prior = [(c, a) for c in calls for a in c['admissions']
                 if a['result'] == 0 and a['sound']['address'] == instance and a['seq'] < start['seq']]
        end = next((e for e in ends if e['seq'] > start['seq'] and e.get('callback') and
                    e['callback']['sound']['address'] == instance), None)
        cool = next((e for e in cooldowns if end and e['seq'] > end['seq'] and e['unit'] == unit), None)
        if not prior or not end or not cool: continue
        if callback['slot'] != 0 or end['callback']['slot'] not in (1, 3):
            violations.append({'error': 'unexpected portrait callback slots', 'seq': start['seq']})
        c, admission = max(prior, key=lambda pair: pair[1]['seq'])
        lifecycle.append({'label': c['request']['label'], 'unit': unit, 'instance': instance,
                          'admission_seq': admission['seq'], 'start_seq': start['seq'], 'end_seq': end['seq'],
                          'cooldown_seq': cool['seq'], 'start_slot': callback['slot'], 'end_slot': end['callback']['slot'],
                          'queued_to_start_ms': start['ticks'] - admission['ticks'],
                          'playing_ms': end['ticks'] - start['ticks'], 'end_to_cooldown_ms': cool['ticks'] - end['ticks']})
    return {'counts': dict(collections.Counter(e.get('event', e.get('type')) for e in events)),
            'variant_rollbacks': rollback, 'rejected_what_counts': rejected_counts,
            'shared_file_policies': aliases, 'playback_notifications': lifecycle, 'violations': violations,
            'scope': 'Observed retail traces only; not an end-to-end OpenRealm parity verdict.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--require-complete', action='store_true')
    parser.add_argument('--require-preemption', action='store_true')
    parser.add_argument('--response-probe', type=Path, help='include production server state diagnostic; not a playback replay')
    args = parser.parse_args()
    raw = args.trace.read_bytes()
    result = analyze([json.loads(line) for line in raw.splitlines()])
    if args.response_probe:
        result['openrealm_queue_state'] = json.loads(subprocess.check_output([str(args.response_probe.resolve())], text=True))
    result['trace_sha256'] = hashlib.sha256(raw).hexdigest()
    result['missing_witnesses'] = [k for k in ('variant_rollbacks', 'rejected_what_counts', 'shared_file_policies', 'playback_notifications') if not result[k]]
    if args.require_preemption and not any(w['end_slot'] == 3 for w in result['playback_notifications']):
        result['violations'].append({'error': 'missing preemption callback witness'})
    text = json.dumps(result, indent=2) + '\n'
    if args.output: args.output.write_text(text)
    else: print(text, end='')
    raise SystemExit(bool(result['violations'] or args.require_complete and result['missing_witnesses']))


if __name__ == '__main__': main()
