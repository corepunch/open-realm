#!/usr/bin/env python3
"""Verify archived public formation passage and regenerate its native-word trace."""
import argparse
import ctypes
import subprocess
import tempfile
import hashlib
import importlib.util
import json
import re
from pathlib import Path


def passage(records):
    r = records
    g=next(x['group'] for x in r if x.get('event')=='tick'); visits=[]
    for x in r:
     if x.get('event')=='tick' and x['group']==g:visits.append({'before':x,'events':[]})
     elif visits and x.get('group')==g:visits[-1]['events'].append(x)
    first=visits[0]['before']; ids={tuple(m['identity']):i for i,m in enumerate(first['members'])}; movers={m['mover']:ids[tuple(m['identity'])] for m in first['members']}
    # Positive software addition for the final supplied owner clock only. The
    # preceding 338 intervals independently match six native 5ms increments.
    def add(a,b):
     ea=(a>>23)&255;eb=(b>>23)&255;e=min(ea,eb)
     m=((a&0x7fffff)|0x800000)<<(ea-e);m+=((b&0x7fffff)|0x800000)<<(eb-e)
     shift=max(0,m.bit_length()-24);m>>=shift
     return ((e+shift)<<23)|(m&0x7fffff)
    def advance(a):
     for _ in range(6):a=add(a,0x3ba3d70a)
     return a
    valid=[v for v in visits if any('mover' in m for m in v['before']['members'])]
    owner=[]
    for i,v in enumerate(valid):
     if i+1<len(valid):owner.append(next(m['time'] for m in valid[i+1]['before']['members'] if 'time' in m))
     else:owner.append(advance(owner[-1]))
    assert owner[0]==next(x['input'][8] for x in r if x.get('event')=='layout-input')
    assert all(advance(a)==b for a,b in zip(owner,owner[1:]))
    rows=[]
    for i,v in enumerate(valid):
     t=v['before'];m=list(t['members']);j=0
     while j<len(m):
      if 'mover' not in m[j]:m[j]=m[-1];m.pop()
      else:j+=1
     members=[[ids[tuple(x['identity'])],*x['offset'],*x['dest'],x['speed'],x['heading'],int(x['flags'],16),*x['pos'],*x['vel'],x['max'],x['facing'],x['radius'],x['range'],x['time'],x['epoch']] for x in m]
     commits=[[movers[x['mover']],int(x['gflags'],16),int(x['mflags'],16),x['req'],x['heading'],x['cap'],x['speed'],*x['pos'],*x['vel'],x['facing'],x['time'],x['epoch']] for x in v['events'] if x['event']=='commit']
     route=next(x for x in v['events'] if x['event']=='route')
     rows.append(dict(clock=owner[i],group=[int(t['flags'],16),t['age'],t['completion'],t['cooldown'],*t['point'],t['heading']],members=members,commits=commits,route=[route['result'],route['ready'],route['final'],route['path']['accCount'],route['path']['accIndex']],regroup=[x for x in v['events'] if x['event']=='regroup'],layouts=[x for x in v['events'] if x['event']=='layout']))
    def carray(a):return '{'+','.join('0x%08xu'%x for x in a)+'}'
    h=['/* Frozen original FORM-05.2 passage: pre-owner/pre-commit observations. */','static struct {uint32_t clock, count, commits;uint32_t group[7],members[6][18],commit[6][14],route[5],regroup[5];} const formation169_passage[]={']
    for x in rows:h.append(' {'+','.join([hex(x['clock'])+'u',str(len(x['members'])),str(len(x['commits'])),carray(x['group']),'{'+','.join(carray(m) for m in x['members'])+'}','{'+','.join(carray(m) for m in x['commits'])+'}',carray(x['route']),carray([1,x['regroup'][0]['result'],x['regroup'][0]['out1'],x['regroup'][0]['out2'],x['regroup'][0]['completion']] if x['regroup'] else [0]*5)])+'},')
    h+=['};','static uint32_t const formation169_goal[]={0x42480006u,0x4200000du};']
    finish=next(x['value'] for x in r if x.get('event')=='marker' and 'label=complete' in x['value'])
    points=[finish.split('u%d='%i)[1].split()[0].rsplit(',',1)[0] for i in range(6)]
    h.append('static char const *const formation169_finish[]={'+','.join(json.dumps(x) for x in points)+'};')
    return "\n".join(h)+"\n",rows


def load_tool(name):
    path = Path(__file__).resolve().parents[2]/'frida/research'/name
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify(frozen, archive, header):
    loaded = {}
    for name, pin in frozen['captures'].items():
        raw = (archive/name).read_bytes()
        if len(raw) != pin['bytes'] or hashlib.sha256(raw).hexdigest() != pin['sha256']:
            raise ValueError('capture pin differs: '+name)
        rows = [json.loads(line) for line in raw.decode().splitlines()]
        meta = rows[0]
        if meta != pin['metadata'] or meta['sha256'] != frozen['binary_sha256'] or not meta['owned']:
            raise ValueError('capture metadata differs: '+name)
        end = [x for x in rows if x['event'] == 'preload-file']
        if len(end) != 1 or not end[0]['complete'] or end[0] != pin['preload']:
            raise ValueError('public script incomplete: '+name)
        preload = (archive/name).with_name(Path(name).stem+'-preload.txt').read_bytes()
        markers = re.findall(r'F05 tick=\d+ label=([^\s\"]+)', preload.decode(errors='replace'))
        if hashlib.sha256(preload).hexdigest() != end[0]['sha256'] or len(markers) != end[0]['markers'] or markers[-1] != 'complete':
            raise ValueError('preload file differs or script did not complete: '+name)
        if meta['mode'] == 'observe':
            stop = [x for x in rows if x['event'] == 'trace-end']
            if len(stop) != 1 or not stop[0]['installed']:
                raise ValueError('missing installed observer completion')
            counts = stop[0]['counts']
            for event, count in counts.items():
                if sum(x['event'] == event for x in rows) != count:
                    raise ValueError('truncated observer event: '+event)
            if not any(x['event'] == 'marker' and 'label=complete' in x['value'] for x in rows):
                raise ValueError('missing public completion marker')
        loaded[name] = rows
    source = frozen['passage']
    regenerated, rows = passage(loaded[source])
    if header.read_text() != regenerated:
        raise ValueError('native-word engine fixture differs from complete captured passage')
    if len(rows) != 340 or sum(len(x['commits']) for x in rows) != 1825:
        raise ValueError('incomplete passage trajectory')
    if sum(len(x['regroup']) for x in rows) != 92 or sum(len(x['layouts']) for x in rows) != 2:
        raise ValueError('incomplete regroup/layout lifetime')
    regroup = load_tool('form052_regroup_timing.py')
    layout = load_tool('form052_layout_compare.py')
    timeline = []; layouts = []
    for name, records in loaded.items():
        if records[0]['mode'] != 'observe':
            continue
        timeline.extend(regroup.run(archive/name))
        layouts.extend(x for x in layout.cases(archive/name) if 'input' in x)
    calls = sum(x['regroup_calls'] for x in timeline)
    if calls != 2491 or any(x['violations'] for x in timeline) or len(layouts) != 23:
        raise ValueError('delivered cross-capture timing/layout contract differs')
    layout_words = [[x['input'], x['live']] for x in layouts]
    if hashlib.sha256(json.dumps(layout_words, separators=(',', ':')).encode()).hexdigest() != frozen['layout_words_sha256']:
        raise ValueError('delivered raw layout words differ')
    root = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix='wc3-formation169-') as directory:
        for optimize in ('-O0', '-O2'):
            engine = Path(directory)/(optimize+'.so')
            subprocess.run(['cc', optimize, '-shared', '-fPIC', '-I', str(root),
                            str(root/'tools/ghidra/wc3_pathing_engine_probe.c'),
                            '-o', str(engine), '-lm'], check=True)
            lib = ctypes.CDLL(str(engine))
            lib.pathing_formation_retail.argtypes = [ctypes.POINTER(ctypes.c_uint32)]*2
            for case in layouts:
                words, expected = case['input'], case['live']
                actual = (ctypes.c_uint32*(1+len(expected)))()
                lib.pathing_formation_retail((ctypes.c_uint32*len(words))(*words), actual)
                if list(actual) != [1]+expected:
                    raise ValueError('live formation layout differs from production C '+optimize)
    return dict(status='live-exact-formation-passage', passed=True, captures=len(loaded),
                owner_visits=340, member_commits=1825, regroup_calls=calls, layouts=len(layouts),
                saved_suffix_visits=260, saved_suffix_commits=sum(len(x['commits']) for x in rows[80:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        result = verify(json.loads(args.expected.read_text()), args.archive, args.header)
    except (ValueError, KeyError, IndexError, OSError, AssertionError, StopIteration, subprocess.CalledProcessError) as error:
        result = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
