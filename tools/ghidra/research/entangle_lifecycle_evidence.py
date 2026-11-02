"""Literal occupant and interruption witnesses for the retained Entangle owner."""
from collections import Counter
import gzip
import json
import re

from entangle_producer_evidence import CONTROL, NAMES, ROOT, SHA, digest, literal, require, word

BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-entangle-lifecycle-1.27.json.gz'
SETTINGS = {
    'occupants': (333, 280, 6, '52e03d71bf08120db208f44be19b911b9b67eae60f33f87328ce8adce7795caa'),
    'interruptions': (500, 322, 7, '10a01104f3001ee7826f062f95b6749e6972e92835c8b1c47418e1abf7f50558'),
}
SITES = [(1024, 960), (1024, 1024), (1088, 1216), (704, 1024), (768, 832), (768, 768)]


def public_contract(markers, setting, scenes, count):
    require(len(markers) == count and markers[-1] ==
            f'TEN tick={scenes * 1000 - 1} scene={scenes} label=complete', 'incomplete lifecycle public stream')
    public = [dict(token.split('=', 1) for token in value.split()[1:]) for value in markers]
    for scene in range(scenes):
        rows = [row for row in public if row['scene'] == str(scene)]
        def one(label):
            found = [row for row in rows if row['label'] == label]
            require(len(found) == 1, 'missing lifecycle marker ' + label)
            return found[0]
        before, issue, end = one('before'), one('entangle-return'), one('settled')
        require(before['head'] == '0' and issue['accepted'] == '1' and issue['head'] == '852147',
                'lifecycle admission differs')
        require(end['head'] == '0', 'lifecycle terminal head differs')
        if setting == 'occupants':
            site = SITES[scene]
            require((end['x'], end['y']) == tuple(f'{v:.3f}' for v in site) and
                    end['overlays'] == '1' and end['minevisible'] == '0', 'occupant root site differs')
            if scene in (1, 2):
                require((before['wx'], before['wy']) == ('1024.000', '960.000'), 'occupant source differs')
        else:
            require(end['overlays'] == ('1' if scene in (0, 3) else '0'), 'interruption overlay differs')
            if scene == 0:
                require((end['x'], end['y']) == ('896.000', '960.000'), 'control root site differs')
            elif scene in (1, 2):
                stopped = one('stop-unroot' if scene == 1 else 'stop-approach')
                require(stopped['accepted'] == '1' and stopped['head'] == '0' and
                        (stopped['x'], stopped['y']) == (end['x'], end['y']), 'Stop failed to retire owner')
            elif scene == 3:
                paused, resumed = one('pause'), one('unpause')
                require(paused['head'] == resumed['head'] == '851973' and
                        (paused['x'], paused['y']) == (resumed['x'], resumed['y']) and
                        (end['x'], end['y']) == ('896.000', '896.000'), 'resume placement differs')
            elif scene in (4, 5):
                samples = [row for row in rows if row['label'] == 'sample' and
                           320 <= int(row['tick']) % 1000 <= 400]
                require(samples and all(row['head'] == '852147' for row in samples) and
                        (end['x'], end['y']) == ('881.388', '964.831'), 'target loss cancelled approach early')
            else:
                require(one('replace-move')['head'] == '851986' and
                        (end['x'], end['y']) == ('521.862', '779.334'), 'replacement Move differs')


def audit(bundle=None):
    data = bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
    require(data['version'] == 1 and data['task'] == 'TARGET-01.2' and data['binary_sha256'] == SHA and
            data['engine_parity'] is False and set(data['settings']) == set(SETTINGS), 'wrong lifecycle corpus')
    for setting, sample in data['settings'].items():
        distance, count, scenes, map_sha = SETTINGS[setting]
        require(sample['range'] == distance and digest(literal(sample['map'], 'raw')) == map_sha,
                'lifecycle range/map changed')
        require(len(sample['captures']) == 3, 'two lifecycle observations and control required')
        streams = []
        for index, capture in enumerate(sample['captures']):
            rows = [json.loads(line) for line in literal(capture, 'raw').splitlines()]
            preload = literal(capture, 'preload')
            header, footer = rows[0], rows[-1]
            mode = 'control' if index == 2 else 'observe'
            env = header['env']
            require(header['event'] == 'metadata' and header['mode'] == mode and header['owned'] and
                    header['sha256'] == SHA and header['task'] == 'TARGET-01.2-entangle' and
                    header['prefix'] == 'TEN ', 'wrong owned lifecycle capture')
            require(env in ('B', 'C') and header['display'] == {'B': ':98', 'C': ':99'}[env] and
                    header['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[env],
                    'wrong lifecycle research environment')
            for name in NAMES:
                source = ROOT / 'tools/ghidra/fixtures/research' / ('entangle-' + setting) / name
                require(header['source_sha256'][name] == digest(source.read_bytes()), 'lifecycle source changed')
            require(header['source_sha256']['map'] == map_sha, 'captured lifecycle map changed')
            require(not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows),
                    'failed lifecycle observer')
            markers = re.findall(r'call Preload\( "(TEN [^"\r\n]*)" \)', preload.decode())
            public_contract(markers, setting, scenes, count)
            require(footer == dict(event='preload-file', sha256=digest(preload), markers=count, complete=True),
                    'incomplete lifecycle preload')
            if mode == 'observe':
                require([row['value'] for row in rows if row.get('event') == 'marker'] ==
                        ['TEN tick=0 scene=-1 label=start'] + markers, 'lifecycle observer/preload differs')
                ends = [row for row in rows if row.get('event') == 'trace-end']
                require(len(ends) == 1 and ends[0]['installed'], 'lifecycle observer never completed')
                counts = ends[0]['counts']
                require(Counter(row['event'] for row in rows if row['event'] in counts) == Counter(counts),
                        'lifecycle observer counts differ')
                for scene in range(scenes):
                    part = [row for row in rows if row.get('scene') == scene]
                    placements = [row for row in part if row['event'] == 'entangle-placement']
                    sites = [SITES[scene]] if setting == 'occupants' else [(896, 960)]
                    if setting == 'interruptions' and scene == 3:
                        sites.append((896, 896))
                    require(len(placements) == len(sites) and all(
                        row['range'] == word(distance) and row['result'] == 1 and
                        row['point'] == [word(v) for v in site] and
                        row['approach'] == (0 if setting == 'occupants' and scene == 3 else 1)
                        for row, site in zip(placements, sites)), 'lifecycle retained placement differs')
                    facing = [row for row in part if row['event'] == 'point-task' and row['code'] == 0xd018f]
                    expected = 1 if setting == 'occupants' or scene in (0, 3) else 0
                    require(len(facing) == expected and all(row['range'] == 0 and
                            row['point'] == [word(v) for v in sites[-1]] for row in facing),
                            'lifecycle Root continuation differs')
            else:
                require(all(row.get('event') in CONTROL and 'seq' not in row for row in rows),
                        'lifecycle control has observer hooks')
            streams.append(markers)
        require(streams[0] == streams[1] == streams[2], 'lifecycle repeats/control differ')
    return dict(observations=4, controls=2, public_markers=1806, engine_parity=False,
                remaining_owner='TARGET-01.2')
