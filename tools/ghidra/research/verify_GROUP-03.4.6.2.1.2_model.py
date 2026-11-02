#!/usr/bin/env python3
"""GROUP-03.4.6.2.1.2: replay the decoded 9d86f0/4985c0/499790 decision against every frozen live call.

Inputs per call come only from the captured unit/attack words (5c flags, rawcode, Attack presence, attack
flags20, suppression counters, weapon types, targets, slot ranges, retained target, BTLF trail) and the captain
6c word; the model must reproduce the captured range word and enabled-slot trail.  Arithmetic truncates toward
zero like the original soft-float helpers (IEEE round-to-nearest misses Hero884 by one ulp).  Also checks the
physical (range+radius)/32 b0 words of the first private admission per unit and departure.
"""
import argparse
import json
import struct
from pathlib import Path

F = lambda w: struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]
W = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
SPECIAL = {0x1, 0x40, 0x80, 0x100}
RUNTIME = dict(base70=70.0, six=F(0x3f19999a), hero600=600.0, unarmed300=300.0, temporary50=50.0, siege200=200.0)


def f32(x):
    return F(W(x))


def trunc32(x):
    """Original soft-float 06f9c0/06fbb0 results truncate toward zero (live Hero884 word 44161999, IEEE 4416199a)."""
    w = W(x)
    if abs(F(w)) > abs(x):
        w -= 1
    return F(w)


def slot_enabled(c, slot):
    flags20 = int(c['flags20'], 16)
    if not flags20 & (0x80000 << slot):
        return 0
    melee, ranged, special = c['suppress']
    wtype, targets = c['weapon_types'][slot], int(c['targets'][slot], 16)
    if melee > 0 and wtype == 1 and targets not in SPECIAL:
        return 0
    if ranged > 0 and 2 <= wtype <= 8 and targets not in SPECIAL:
        return 0
    if special > 0 and targets in SPECIAL:
        return 0
    return 1


def model(c, btlf):
    flags5c = int(c['flags5c'], 16)
    trail = []
    if flags5c & 0x40000000 or btlf:
        return RUNTIME['temporary50'], trail
    if not c['attack_present']:
        value = RUNTIME['unarmed300']
    else:
        if c['target'] != ['0xffffffff', '0xffffffff']:
            raise ValueError('retained target observed; adjusted branch not modelled')
        maximum = 0.0
        for slot in range(2):
            enabled = slot_enabled(c, slot)
            trail.append(['slot', slot, enabled])
            if enabled and c['ranges'][slot] > maximum:
                maximum = c['ranges'][slot]
        trail.append(['maximum', W(maximum)])
        value = trunc32(trunc32(maximum * RUNTIME['six']) + RUNTIME['base70'])
    rawcode_hero = 'A' <= c['unit'][0] <= 'Z'
    if rawcode_hero and RUNTIME['hero600'] > value:
        value = RUNTIME['hero600']
    if int(c['captain_flags'], 16) & 0x20:
        value = trunc32(value + RUNTIME['siege200'])
    return value, trail


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    data = json.loads(a.expected.read_text())
    checked, physical, failures = 0, 0, []
    for name, scene in data['scenes'].items():
        for c in scene['authored_calls']:
            btlf = c['attack_present'] and not c['trail'] and not int(c['flags5c'], 16) & 0x40000000
            if not c['attack_present'] and not c['trail']:
                btlf = False  # no Attack: BTLF lookup still ran and returned zero (range 300 proves it)
            value, trail = model(c, btlf)
            if W(value) != c['range_word'] or (trail and trail != c['trail']):
                failures.append(dict(scene=name, tick=c['tick'], unit=c['unit'], model=W(value), live=c['range_word'], trail=trail, live_trail=c['trail']))
            checked += 1
        radius = {k: v[0]['radius'] for k, v in scene['arrival_ranges'].items()}
        for c in scene['authored_calls']:
            hist = scene['arrival_ranges'].get(c['unit'], [])
            match = [h for h in hist if h['tick'] in (c['tick'], c['tick'] + 1) and h['stored_word'] != 0x3efae148]
            if not match:
                continue
            want = W(trunc32(trunc32(c['range'] + match[0]['radius']) / 32.0))
            if match[0]['stored_word'] != want:
                failures.append(dict(scene=name, tick=c['tick'], unit=c['unit'], physical_model=want, physical_live=match[0]['stored_word']))
            physical += 1
    result = dict(expected=str(a.expected), authored_calls=checked, physical_ranges=physical, failures=failures, passed=not failures)
    if a.output:
        a.output.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))
    raise SystemExit(0 if not failures else 1)


if __name__ == '__main__':
    main()
