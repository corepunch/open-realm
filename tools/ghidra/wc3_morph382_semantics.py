"""Literal semantic contracts extracted from accepted Payoff382 verifiers.
No engine algorithms or source-data inference. Original verifier bytes remain frozen.
"""
import re

ordinary_MARK=re.compile('P382(D?) setting=([01]) invisible=([01]) tick=(\\d+) scene=(\\d+) label=([a-z-]+) accepted=([01]) type=(\\d+) morph=(\\d+) passive=(\\d+) buff=(\\d+) order=(\\d+)')

def ordinary_fields(v):
    m = ordinary_MARK.fullmatch(v)
    if not m:
        raise ValueError('unknown full marker')
    return dict(zip(['detach', 'setting', 'invisible', 'tick', 'scene', 'label', 'accepted', 'type', 'morph', 'passive', 'buff', 'order'], [bool(m[1])] + [m[i] if i == 6 else int(m[i]) for i in range(2, 13)]))

def ordinary_semantic(rows, markers, setting, detach):
    samples = [ordinary_fields(v) for v in markers if ' label=start' not in v]
    if not all((x['setting'] == setting and x['detach'] == detach for x in samples)):
        raise ValueError('native semantic contract differs')
    if not [r['result'] for r in rows if r['event'] == 'morph-policy-leave'] == [setting] * (9 if detach else 14):
        raise ValueError('native semantic contract differs')
    if not [r['delay'] for r in rows if r['event'] == 'transition-timer'] == [1052770304] * (2 if detach else 5):
        raise ValueError('native semantic contract differs')
    if not [r['delay'] for r in rows if r['event'] == 'form-lifetime-timer'] == [1075838976] * (2 if detach else 3):
        raise ValueError('native semantic contract differs')
    if not [r['duration'] for r in rows if r['event'] == 'metamorphosis-buff-attach-enter'] == [1075880919] * (2 if detach else 3):
        raise ValueError('native semantic contract differs')
    positive = [s for s in samples if s['label'] == 'pre-forward-visibility']
    if not (len(positive) == (2 if detach else 1) and all((s['invisible'] == 1 for s in positive))):
        raise ValueError('native semantic contract differs')
    if not all((x['accepted'] == 1 for x in samples if x['label'] in ['forward', 'detach-morph', 'add-morph', 'add-passive-before', 'add-passive-after'])):
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'morph-form-disable-leave'] == ([12, 85] if detach else [15, 85, 140]):
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'morph-form-enable-leave'] == ([12, 91] if detach else [44, 114, 169]):
        raise ValueError('native semantic contract differs')
    for tick in ([13, 92] if setting == 0 else [20, 99]) if detach else [45, 115, 170] if setting == 0 else [51, 121, 176]:
        if not any((s['tick'] == tick and s['invisible'] == 1 and (s['type'] == 1164207469) for s in samples)):
            raise ValueError('missing actual postinverse fade witness')
    if detach:
        leaves = [r for r in rows if r['event'] == 'metamorphosis-detach-leave']
        if not len(leaves) == 2:
            raise ValueError('native semantic contract differs')
        if not all((r['state']['rawcode'] == 1164207469 and r['state']['disableDepth'] == 0 and (r['state']['invisibilityDepth'] == 1) for r in leaves)):
            raise ValueError('native semantic contract differs')
        if not all((bool(any(r['state']['invisibilityScalar'][4:])) == bool(setting) for r in leaves)):
            raise ValueError('native semantic contract differs')
        if not all((s['type'] == 1164207469 and s['morph'] == s['buff'] == s['order'] == 0 and (s['passive'] == 1) and (s['invisible'] == 1 - setting) for s in samples if s['label'] == 'detach-morph')):
            raise ValueError('native semantic contract differs')
    elif not [r['tick'] for r in rows if r['event'] == 'metamorphosis-buff-removed-enter'] == [40, 110, 166]:
        raise ValueError('native semantic contract differs')
    return len(samples)

public_MARK=re.compile('P382(R) setting=([01]) invisible=([01]) tick=(\\d+) scene=(\\d+) label=([a-z-]+) accepted=([01]) type=(\\d+) morph=(\\d+) passive=(\\d+) buff=(\\d+) order=(\\d+)')

def public_fields(text):
    m = public_MARK.fullmatch(text)
    if not m:
        raise ValueError('native semantic contract differs')
    return dict(zip(['family', 'setting', 'invisible', 'tick', 'scene', 'label', 'accepted', 'type', 'morph', 'passive', 'buff', 'order'], [m[i] if i in (1, 6) else int(m[i]) for i in range(1, 13)]))

def public_semantic(rows, markers, setting, detach=False):
    samples = [public_fields(x) for x in markers if ' label=start' not in x]
    if not all((s['setting'] == setting for s in samples)):
        raise ValueError('native semantic contract differs')
    if not [r['result'] for r in rows if r['event'] == 'morph-policy-leave'] == [setting] * 14:
        raise ValueError('native semantic contract differs')
    if not [r['delay'] for r in rows if r['event'] == 'transition-timer'] == [1052770304] * 2:
        raise ValueError('native semantic contract differs')
    if not [r['delay'] for r in rows if r['event'] == 'form-lifetime-timer'] == [1075838976] * 3:
        raise ValueError('native semantic contract differs')
    if not [r['duration'] for r in rows if r['event'] == 'metamorphosis-buff-attach-enter'] == [1075880919] * 3:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'metamorphosis-buff-removed-enter'] == [22, 92, 162]:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'morph-form-enable-leave'] == [22, 92, 162]:
        raise ValueError('native semantic contract differs')
    removes = [s for s in samples if s['label'] == 'remove-metamorphosis-buff']
    if not len(removes) == 3:
        raise ValueError('native semantic contract differs')
    if not all((s['accepted'] == 1 and s['type'] == 1164207469 and (s['buff'] == s['order'] == 0) and (s['morph'] == s['passive'] == 1) and (s['invisible'] == 1 - setting) for s in removes)):
        raise ValueError('native semantic contract differs')
    for tick in [22, 92, 162]:
        if not any((s['tick'] == tick and s['buff'] == 1 for s in samples)):
            raise ValueError('missing positive existing BEme before public removal')
    for tick in [23, 93, 163] if setting == 0 else [30, 100, 170]:
        if not any((s['tick'] == tick and s['invisible'] == 1 and (s['type'] == 1164207469) for s in samples)):
            raise ValueError('native semantic contract differs')

alias_MARK=re.compile('P382(A) setting=([01]) invisible=([01]) tick=(\\d+) scene=(\\d+) label=([a-z-]+) accepted=([01]) type=(\\d+) morph=(\\d+) passive=(\\d+) buff=(\\d+) basebuff=(\\d+) order=(\\d+)')

def alias_fields(text):
    m = alias_MARK.fullmatch(text)
    if not m:
        raise ValueError('native semantic contract differs')
    return dict(zip(['family', 'setting', 'invisible', 'tick', 'scene', 'label', 'accepted', 'type', 'morph', 'passive', 'buff', 'basebuff', 'order'], [m[i] if i in (1, 6) else int(m[i]) for i in range(1, 14)]))

def alias_semantic(rows, markers, setting, detach=False):
    samples = [alias_fields(x) for x in markers if ' label=start' not in x]
    if not all((s['setting'] == setting and s['basebuff'] == 0 for s in samples)):
        raise ValueError('native semantic contract differs')
    if not all((r['availability']['class'] == 'b6f6b0' and r['availability']['code'] == 1113746543 for r in rows if r['event'] == 'metamorphosis-buff-attach-leave')):
        raise ValueError('native semantic contract differs')
    if not all((r['source']['class'] == 'b6fa20' and r['source']['code'] == 1095579698 for r in rows if r['event'] == 'metamorphosis-buff-attach-enter')):
        raise ValueError('native semantic contract differs')
    if not [r['result'] for r in rows if r['event'] == 'morph-policy-leave'] == [setting] * 14:
        raise ValueError('native semantic contract differs')
    if not [(r['tick'], r['delay'], r['caller']) for r in rows if r['event'] == 'transition-timer'] == [(11, 1052770304, '569f37')] + [(22, 1059766403, '6d81b8')] * 4 + [(81, 1052770304, '569f37')] + [(92, 1059766403, '6d81b8')] * 4 + [(162, 1059766403, '6d81b8')] * 4:
        raise ValueError('native semantic contract differs')
    if not [r['delay'] for r in rows if r['event'] == 'form-lifetime-timer'] == [1075838976] * 3:
        raise ValueError('native semantic contract differs')
    if not [r['duration'] for r in rows if r['event'] == 'metamorphosis-buff-attach-enter'] == [1075880919] * 3:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'metamorphosis-buff-removed-enter'] == [22, 92, 162]:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'morph-form-enable-leave'] == [22, 92, 162]:
        raise ValueError('native semantic contract differs')
    removes = [s for s in samples if s['label'] == 'remove-metamorphosis-buff']
    if not len(removes) == 3:
        raise ValueError('native semantic contract differs')
    if not all((s['accepted'] == 1 and s['type'] == 1164207469 and (s['buff'] == s['order'] == 0) and (s['morph'] == s['passive'] == 1) and (s['invisible'] == 1 - setting) for s in removes)):
        raise ValueError('native semantic contract differs')
    for tick in [22, 92, 162]:
        if not any((s['tick'] == tick and s['buff'] == 1 for s in samples)):
            raise ValueError('missing positive existing BEme before public removal')
    for tick in [23, 93, 163] if setting == 0 else [30, 100, 170]:
        if not any((s['tick'] == tick and s['invisible'] == 1 and (s['type'] == 1164207469) for s in samples)):
            raise ValueError('native semantic contract differs')

def pause_fields(text):
    if not text.startswith('P382P '):
        raise ValueError('native semantic contract differs')
    out = dict((w.split('=', 1) for w in text.split()[1:]))
    return {k: z if k == 'label' else int(z) for k, z in out.items()}

def pause_semantic(rows, markers, setting, detach=False):
    samples = [pause_fields(x) for x in markers if ' label=start' not in x]
    if not all((s['setting'] == setting for s in samples)):
        raise ValueError('native semantic contract differs')
    bylabel = lambda label: [s for s in samples if s['label'] == label]
    if not (len(bylabel('pre-forward-visibility')) == 2 and all((s['invisible'] == 1 for s in bylabel('pre-forward-visibility')))):
        raise ValueError('native semantic contract differs')
    if not (len(bylabel('pause')) == 4 and all((s['buff'] == s['paused'] == 1 and s['type'] == 1164209517 for s in bylabel('pause')))):
        raise ValueError('native semantic contract differs')
    held = bylabel('held-across-natural-lifetime')
    if not len(held) == 4:
        raise ValueError('native semantic contract differs')
    for s in held:
        if not (s['paused'] == 1 and s['buff'] == int(s['scene'] < 2) and (s['type'] == (1164209517 if s['scene'] < 2 else 1164207469))):
            raise ValueError('native semantic contract differs')
    removes = bylabel('remove-metamorphosis-buff')
    if not (len(removes) == 2 and all((s['accepted'] == s['paused'] == 1 and s['buff'] == 0 and (s['type'] == 1164207469) for s in removes))):
        raise ValueError('native semantic contract differs')
    final = bylabel('post-resume-visibility')
    if not len(final) == 4:
        raise ValueError('native semantic contract differs')
    if not all((s['paused'] == 0 and s['buff'] == int(s['scene'] >= 2) and (s['type'] == (1164209517 if s['scene'] >= 2 else 1164207469)) for s in final)):
        raise ValueError('native semantic contract differs')
    if not [(s['scene'], s['invisible']) for s in final] == ([(0, 1), (1, 1), (2, 1), (3, 1)] if setting == 0 else [(0, 0), (1, 1), (2, 0), (3, 0)]):
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'private-clock-freeze-enter'] == [21, 111, 201, 291]:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'private-clock-resume-enter'] == [54, 144]:
        raise ValueError('native semantic contract differs')
    if not all((r['availability']['clockDepth'] == 1 for r in rows if r['event'] == 'private-clock-freeze-enter')):
        raise ValueError('native semantic contract differs')
    if not all((r['availability']['clockDepth'] == 0 for r in rows if r['event'] == 'private-clock-resume-enter')):
        raise ValueError('native semantic contract differs')
    if not [r['duration'] for r in rows if r['event'] == 'metamorphosis-buff-attach-enter'] == [1075880919] * 6:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'metamorphosis-buff-removed-enter'] == [73, 149, 202, 265, 292, 355]:
        raise ValueError('native semantic contract differs')

def sleep_fields(text):
    if not text.startswith('P382W '):
        raise ValueError('native semantic contract differs')
    out = dict((w.split('=', 1) for w in text.split()[1:]))
    return {k: z if k == 'label' else int(z) for k, z in out.items()}

def sleep_semantic(rows, markers, setting, detach=False):
    samples = [sleep_fields(x) for x in markers if ' label=start' not in x]
    if not all((s['setting'] == setting and s['immune'] == 0 for s in samples)):
        raise ValueError('native semantic contract differs')
    bylabel = lambda label: [s for s in samples if s['label'] == label]
    positive = bylabel('sleep-before-script-pause')
    if not (len(positive) == 4 and all((s['sleep'] == s['guard'] == s['buff'] == 1 and s['paused'] == 0 and (s['order'] == 851973) for s in positive))):
        raise ValueError('native semantic contract differs')
    for scene in range(4):
        if not any((s['scene'] == scene and s['tick'] % 160 < 16 and (s['controlSleep'] == 1) for s in samples)):
            raise ValueError('native semantic contract differs')
    for label in ['remove-sleep-after-unpause', 'remove-sleep-while-paused']:
        removed = bylabel(label)
        if not (len(removed) == 1 and removed[0]['accepted'] == 1 and (removed[0]['sleep'] == 0) and (removed[0]['guard'] == removed[0]['buff'] == 1)):
            raise ValueError('native semantic contract differs')
    if not [s['paused'] for s in bylabel('unpause')] == [0, 0, 0]:
        raise ValueError('native semantic contract differs')
    if not [s['sleep'] for s in bylabel('unpause')] == [1, 1, 0]:
        raise ValueError('native semantic contract differs')
    if not [r['result'] for r in rows if r['event'] == 'morph-policy-leave'] == [setting] * 32:
        raise ValueError('native semantic contract differs')
    if not [(r['tick'], r['state']['pauseDepth'], r['availability']['clockDepth']) for r in rows if r['event'] == 'private-clock-freeze-enter'] == [(21, 2, 1), (181, 2, 1), (341, 2, 1)]:
        raise ValueError('native semantic contract differs')
    if not [(r['tick'], r['state']['pauseDepth'], r['availability']['clockDepth']) for r in rows if r['event'] == 'private-clock-resume-enter'] == [(54, 3, 0), (214, 3, 0), (374, 2, 0)]:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'metamorphosis-buff-removed-enter'] == [73, 233, 393, 520]:
        raise ValueError('native semantic contract differs')
    if not [r['tick'] for r in rows if r['event'] == 'morph-form-enable-leave'] == [77, 237, 397, 524]:
        raise ValueError('native semantic contract differs')
    for tick in [78, 525]:
        sample = next((s for s in samples if s['tick'] == tick and s['label'] == 'sample'))
        if not (sample['type'] == 1164207469 and sample['sleep'] == 1 and (sample['buff'] == 0) and (sample['order'] == 851973)):
            raise ValueError('native semantic contract differs')
