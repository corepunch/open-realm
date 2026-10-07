#!/usr/bin/env python3
"""Validate complete immobile-unit Frida repeats and observer-free markers.

Optionally replay every retained pair/tail through original code. Endpoint and
public producer reachability come from the capture; this replay does not replace
them with synthetic helper evidence.
"""
import gzip
import hashlib
import json
import re
from pathlib import Path


def markers(text):
    return re.findall(r'call Preload\( "(PATHSEP[^"\r\n]*)" \)', text)


def normalize(rows):
    units, movers, output = {}, {}, []
    for row in rows:
        event = row['event']
        if event == 'refresh':
            if row['unit'] not in units:
                units[row['unit']] = len(units)
        elif event == 'configure':
            if row['unit'] is not None and row['unit'] not in units:
                units[row['unit']] = len(units)
            if row['unit'] is not None:
                movers[row['mover']] = units[row['unit']]
            output.append([event, row['tick'], movers.get(row['mover']),
                           row['args'][0], row['args'][1] & 255,
                           row['args'][2] & 65535, row['args'][3] & 255, row['state']])
        elif event == 'sep-update':
            output.append([event, row['tick'], row['visit'], movers[row['mover']],
                           row['before'], row['after'], row['ownerBefore'], row['ownerAfter'],
                           row['moverBefore']['pos'], row['moverAfter']['pos'],
                           row['moverBefore']['speed'], row['pairs']])
    return output


def verify(path, oracle=None):
    data = json.loads(gzip.decompress(Path(path).read_bytes()))
    expected_map = data['map']['sha256']
    assert data['map']['units']['hSI0']['umvs'] == 0
    assert data['map']['units']['hSF0']['umvs'] == 0
    sequences = []
    visits = pairs = 0
    for capture in data['captures']:
        provenance = capture['provenance']
        assert provenance['preload_complete'] and not provenance.get('error')
        assert provenance['map_sha256'] == expected_map
        captured = ''.join(json.dumps(row) + '\n' for row in capture['rows']).encode()
        assert hashlib.sha256(captured).hexdigest() == provenance['capture_sha256']
        text = markers(capture['markers'])
        assert text and 'label=complete' in text[-1]
        assert text == markers(data['control']['markers'])
        sequences.append(normalize(capture['rows']))
        for row in capture['rows']:
            if row['event'] != 'sep-update':
                continue
            visits += 1
            pairs += len(row['pairs'])
            if oracle is None:
                continue
            before, after = row['before'], row['after']
            if before['word'] & 65535:
                assert after == dict(vec=before['vec'], word=before['word'] - 1)
                continue
            assert row['moverBefore']['speed'] == 0
            selector = (before['word'] >> 16) & 15
            vector, owner = before['vec'], row['ownerBefore']
            for pair in row['pairs']:
                assert pair['vecBefore'] == vector and pair['ownerBefore'] == owner
                owner, vector, _ = oracle.pair(oracle.settings[selector], owner,
                                               pair['source'], pair['candidate'], vector)
                assert vector == pair['vecAfter'] and owner == pair['ownerAfter']
            vector, word = oracle.tail(oracle.settings[selector], vector, before['word'])
            assert after == dict(vec=vector, word=word)
            assert owner == row['ownerAfter']
    assert len(sequences) == 2 and sequences[0] == sequences[1]
    assert data['control']['provenance']['preload_complete']
    assert data['control']['provenance']['map_sha256'] == expected_map
    return dict(immobile_visits=visits, immobile_pairs=pairs,
                immobile_markers=len(markers(data['control']['markers'])))
