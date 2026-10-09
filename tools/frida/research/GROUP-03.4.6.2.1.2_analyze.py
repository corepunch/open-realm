#!/usr/bin/env python3
"""GROUP-03.4.6.2.1.2: summarize one observed capture (read-only JSONL -> JSON report).

Groups every original 9d86f0 private-captain-range call by unit identity (rawcode plus the
illusion bit 5c.40000000), with the complete ordered branch trail (BTLF lookup at 9d871e, enabled
maximum at 9d8743, per-slot 499790 results inside 4985c0, adjusted 2cc return), the attack words
read at entry, the returned world range and the Move task range (6926b0 from 5fd92b).  Also emits
roster attach/detach, member reissues, suppression-counter mutations and the stored mover range
(+b0, fine cells) at every JASS sample marker.
"""
import argparse
import collections
import hashlib
import json
import struct
from pathlib import Path

LABELS = {'hRA0': 'control melee90', 'hRA1': 'melee90 melee-prevented pre', 'hRA2': 'missile400 ranged-prevented pre',
          'hRA3': 'missile400 melee-prevented pre', 'hRA4': 'missile400 ranged-prevented post', 'hRA5': 'melee90 BTLF pre',
          'hRA6': 'melee90 BTLF post', 'hRA7': 'melee90 Aatk removed pre', 'hRA8': 'melee90 Aatk removed post',
          'hRA9': 'rifleman Rhri post', 'hRAs': 'tree200 special-prevented pre', 'hRAt': 'tree200 melee-prevented pre',
          'HRA0': 'hero melee100', 'HRA1': 'hero 883', 'HRA2': 'hero 884', 'hRC0': 'caster'}


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xffffffff))[0]


def code(raw):
    return raw.to_bytes(4, 'big').decode('latin1')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    raw = args.capture.read_bytes()
    rows = [json.loads(x) for x in raw.splitlines()]
    tick = 0
    calls, roster, reissues, suppress, tasks, stored, markers = [], [], [], [], [], collections.OrderedDict(), []
    unit_name = {}
    mover_unit = {}
    for r in rows:
        if r.get('event') == 'unit-snapshot':
            for u in r['rows']:
                m = u.get('mover') or {}
                if 'mover' in m:
                    mover_unit.setdefault(m['mover'], u['unit'])
    arrivals = []
    for r in rows:
        e = r.get('event')
        if e == 'marker':
            markers.append(r['value'])
            tick = int(r['value'].split()[1][5:])
        elif e == 'authored-range':
            key = code(r['rawcode']) + ('/illusion' if r['flags5c'] & 0x40000000 else '')
            unit_name[r['unit']] = key
            a = r['attackBefore']
            calls.append(dict(tick=tick, unit=key, label=LABELS.get(code(r['rawcode']), '?'), caller=hex(r['caller']),
                              captain=r['captain'], captain_flags=hex(r['captainFlags']), flags5c=hex(r['flags5c']),
                              attack=r['attackPtr'], trail=r['trail'], range_word=r['range'], range=f32(r['range']),
                              suppress=a and a['suppress'], weapon_types=a and a['weaponTypes'], targets=a and [hex(v) for v in a['targets']],
                              ranges=a and [f32(v) for v in a['ranges']], flags20=a and hex(a['flags20']),
                              target=a and [hex(v) for v in a['target']], adjusted=a and f32(a['adjusted']),
                              constants=[f32(v) for v in r['constants']], mover=r['mover']))
        elif e == 'roster':
            unit_name.setdefault(r['unit'], code(r['rawcode']) + ('/illusion' if r['flags5c'] & 0x40000000 else ''))
            roster.append(dict(tick=tick, name=r['name'], unit=unit_name[r['unit']], captain=r['captain'],
                               captain_flags=hex(r['captainFlags']), caller=hex(r['caller'])))
        elif e == 'reissue':
            reissues.append(dict(tick=tick, unit=unit_name.get(r['unit'], r['unit']), order=hex(r['order']), caller=hex(r['caller'])))
        elif e == 'suppression':
            suppress.append(dict(tick=tick, attack=r['attack'], release=r['release'], masks=[r['melee'], r['ranged'], r['special']],
                                 before=r['before'], after=r['after'], caller=hex(r['caller'])))
        elif e == 'task-range':
            tasks.append(dict(tick=tick, unit=unit_name.get(r['unit'], r['unit']), kind=hex(r['kind']), range=f32(r['range'])))
        elif e == 'arrival-range':
            arrivals.append(dict(tick=tick, mover=r['mover'], unit=r['mover'] if r['mover'] not in mover_unit else mover_unit[r['mover']],
                                 radius=f32(r['radius']) * 32, stored=f32(r['storedRange']), stored_word=r['storedRange']))
        elif e == 'unit-snapshot':
            for u in r['rows']:
                m = u.get('mover') or {}
                if 'storedRange' not in m:
                    continue
                key = unit_name.get(u['unit'], code(u['rawcode']))
                value = (u['attackPtr'], hex(u['flags5c']), m['radius'], m['storedRange'])
                hist = stored.setdefault(key, [])
                if not hist or hist[-1][1:] != list(value):
                    hist.append([tick, *value])
    for k, hist in stored.items():
        for h in hist:
            h.append(f32(h[3]) * 32)  # radius world
            h.append(f32(h[4]))       # stored range fine cells
    report = dict(capture=str(args.capture), sha256=hashlib.sha256(raw).hexdigest(), rows=len(rows),
                  complete=any(' label=complete' in m for m in markers), footer=rows[-1],
                  authored_calls=calls, roster=roster, reissues=reissues, suppression=suppress, task_ranges=tasks,
                  stored_ranges=stored, arrivals=[dict(a, unit=unit_name.get(a['unit'], a['unit'])) for a in arrivals], markers=[m for m in markers if ' label=sample ' not in m])
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    by = collections.OrderedDict()
    for c in calls:
        by.setdefault(c['unit'], []).append((c['tick'], c['caller'], round(c['range'], 4), c['suppress'], c['trail']))
    for k, v in by.items():
        print(k, LABELS.get(k.split('/')[0], '?'), v[:3], '... n=%d' % len(v))
    print('roster', len(roster), 'reissues', len(reissues), 'suppression', len(suppress), 'tasks', len(tasks))


if __name__ == '__main__':
    main()
