#!/usr/bin/env python3
"""Build RS-SEP research maps (SEP-01.2..SEP-04.3) from the flat 64x64 passage map.

Base: <research data>/Maps/PathingRE-MovementBypasses86b-261003.w3m (sha256 4b9ea0ba...6555), the same
base used by the accepted Payoff101 random-interleave map. Only war3map.j (probe region), war3map.wpm,
war3map.w3u (appended custom rows), war3map.shd (zeroed, as Payoff101) and an added war3map.w3a change.
Never overwrites an existing map. World coordinates span 0..2048 (64 fine cells of 32 world units).
"""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path

BASE_SHA = '4b9ea0ba54a0c4425455282931a8d2b7d0160c7c043cf02ccfdcf0381a036555'

# Custom unit rows: hfoo clones. Fields: ucol(real), urpo/urpp/urpg/urpr(int), umvt(string), umvh(real).
UNITS = {
    'hS00': dict(urpo=1), 'hS01': dict(urpo=1, urpp=1), 'hS02': dict(urpo=1, urpp=2), 'hS03': dict(urpo=1, urpp=3),
    'hS04': dict(urpo=1, urpp=4), 'hS05': dict(urpo=1, urpp=5), 'hS17': dict(urpo=1, urpp=17),
    'hSG1': dict(urpo=1, urpg=1), 'hSGH': dict(urpo=1, urpg=17), 'hSR1': dict(urpo=1, urpr=1),
    'hSR2': dict(urpo=1, urpr=2), 'hSRH': dict(urpo=1, urpr=17), 'hSE2': dict(urpo=2), 'hSD0': dict(),
    'hSFL': dict(urpo=1, umvt='fly', umvh=60.0), 'hSHO': dict(urpo=1, umvt='hover'), 'hSAM': dict(urpo=1, umvt='amph'),
    'hSHR': dict(urpo=1, umvt='horse'), 'hSC1': dict(urpo=1, ucol=16.0), 'hSC2': dict(urpo=1, ucol=32.0),
    'hSD1': dict(ucol=16.0),
}


def w3u_rows(data):
    version = struct.unpack_from('<I', data)[0]
    if version != 1:
        raise ValueError('requires version1 unit modifications')
    cursor, ids = 4, set()
    for table in range(2):
        count_at = cursor
        count = struct.unpack_from('<I', data, cursor)[0]; cursor += 4
        for _ in range(count):
            old, new, n = struct.unpack_from('<4s4sI', data, cursor); cursor += 12; ids.add(new)
            for _ in range(n):
                field, kind = struct.unpack_from('<4sI', data, cursor); cursor += 8
                cursor = data.index(b'\0', cursor) + 1 if kind == 3 else cursor + 4
                cursor += 4
    if cursor != len(data):
        raise ValueError('trailing unit modification data')
    out = data[:count_at] + struct.pack('<I', count + len(UNITS)) + data[count_at + 4:]
    for code, fields in UNITS.items():
        c = code.encode()
        if c in ids:
            raise ValueError('custom id exists: ' + code)
        mods = [(b'ucol', 2, struct.pack('<f', fields.get('ucol', 8.0)))]
        for f in ('urpo', 'urpp', 'urpg', 'urpr'):
            if f in fields:
                mods.append((f.encode(), 0, struct.pack('<i', fields[f])))
        if 'umvt' in fields:
            mods.append((b'umvt', 3, fields['umvt'].encode() + b'\0'))
        if 'umvh' in fields:
            mods.append((b'umvh', 2, struct.pack('<f', fields['umvh'])))
        if 'umvs' in fields:
            mods.append((b'umvs', 0, struct.pack('<i', fields['umvs'])))
        out += b'hfoo' + c + struct.pack('<I', len(mods))
        for f, kind, value in mods:
            out += f + struct.pack('<I', kind) + value + c
    return out


def w3a_channel():
    """'ASch' = Channel (ANcl) clone: no target, visible, 3 s follow-through, order 'channel', free."""
    mods = [(b'Ncl1', 2, 1, 1, struct.pack('<f', 3.0)), (b'Ncl2', 0, 1, 2, struct.pack('<i', 0)),
            (b'Ncl3', 0, 1, 3, struct.pack('<i', 1)), (b'Ncl4', 2, 1, 4, struct.pack('<f', 0.0)),
            (b'Ncl5', 0, 1, 5, struct.pack('<i', 0)), (b'Ncl6', 3, 1, 6, b'channel\0'),
            (b'amcs', 0, 1, 0, struct.pack('<i', 0)), (b'acdn', 2, 1, 0, struct.pack('<f', 0.0)),
            (b'aher', 0, 0, 0, struct.pack('<i', 0))]
    out = struct.pack('<III', 2, 0, 1) + b'ANcl' + b'ASch' + struct.pack('<I', len(mods))
    for f, kind, level, dp, value in mods:
        out += f + struct.pack('<III', kind, level, dp) + value + b'ASch'
    return out


def wpm(blocked):
    cells = bytearray(64 * 64)
    for x, y in blocked:
        cells[y * 64 + x] = 0xc6
    return b'MP3W' + struct.pack('<III', 0, 64, 64) + bytes(cells)


GRID = [304.0, 1024.0, 1744.0]


def centers():
    return [(x, y) for y in GRID for x in GRID]


def pair(a, pa, b, pb, d=8.0, actions=()):
    return dict(units=[(a, pa, -d / 2, 0.0), (b, pb, d / 2, 0.0)], actions=list(actions))


def tri(codes, owners, offs, actions=()):
    return dict(units=[(c, o, dx, dy) for c, o, (dx, dy) in zip(codes, owners, offs)], actions=list(actions))


TRI_OFFS = [(0.0, 0.0), (9.6, 3.2), (-6.4, 11.2)]
COLLAPSE = 'call SetUnitX({0},GetUnitX({1})-8.0)\n call SetUnitY({0},GetUnitY({1}))'


def variant_phases(name):
    if name == 'immobile':
        return [([
            pair('hSI0', 0, 'hSI0', 0),
            pair('hSI0', 0, 'hS00', 0),
            pair('hSI0', 0, 'hSI0', 1, actions=[(40, 'call SetUnitOwner({1},Player(0),false)'),
                (70, 'call PauseUnit({1},true)'), (90, 'call PauseUnit({1},false)')]),
            pair('hSF0', 0, 'hSFL', 0),
            pair('hSF0', 0, 'hSF0', 0),
            pair('hSD0', 0, 'hS00', 0),
        ], 140)], []
    if name == 'policy':
        A = [pair('hS00', 0, 'hS00', 0), pair('hS00', 0, 'hS00', 1), pair('hS00', 0, 'hSD0', 0), pair('hS00', 0, 'hSG1', 0),
             pair('hSG1', 0, 'hSGH', 0), pair('hSR1', 0, 'hS00', 0), pair('hSR2', 0, 'hSR1', 0), pair('hSRH', 0, 'hSR1', 0),
             pair('hSE2', 0, 'hS00', 0)]
        B = [pair('hS0%d' % k, 0, 'hS0%d' % k, 0, d=192.0) for k in range(5)] + [
             pair('hS05', 0, 'hS05', 0), pair('hS05', 0, 'hS00', 0), pair('hS17', 0, 'hS01', 0), pair('hSD0', 0, 'hSD0', 0)]
        C = [pair('hSFL', 0, 'hSFL', 0), pair('hSFL', 0, 'hS00', 0), pair('hSHO', 0, 'hS00', 0), pair('hSAM', 0, 'hS00', 0),
             pair('hSHR', 0, 'hS00', 0), pair('hSHO', 0, 'hSHO', 0), pair('hSFL', 0, 'hSFL', 2), pair('hS00', 15, 'hS00', 15),
             pair('hS00', 15, 'hS00', 0)]
        D = [pair('hS00', 15, 'hS00', 0, actions=[(20, "call UnitAddAbility({1},'Amec')"), (100, 'call SetUnitOwner({1},Player(1),false)')]),
             pair('hS00', 15, 'hS00', 0, actions=[(100, 'call SetUnitOwner({1},Player(1),false)')]),
             pair('hS00', 15, 'hS00', 0, actions=[(20, "call UnitAddAbility({1},'Amec')"), (160, 'call PauseUnit({1},true)'), (180, 'call PauseUnit({1},false)')]),
             pair('hS00', 0, 'hS00', 1, actions=[(60, 'call SetUnitOwner({1},Player(0),false)')]),
             pair('hS00', 0, 'hS00', 0, actions=[(1, "call UnitAddAbility({1},'ASch')"), (60, 'call IssueImmediateOrder({1},"channel")'), (62, COLLAPSE)]),
             pair('hS00', 0, 'hS00', 0, actions=[(60, 'call PauseUnit({1},true)'), (62, COLLAPSE), (140, 'call PauseUnit({1},false)')]),
             pair('hSAM', 0, 'hSAM', 0), pair('hSHR', 0, 'hSHR', 0), pair('hSC2', 0, 'hS00', 0)]
        return [(A, 140), (B, 140), (C, 140), (D, 240)], []
    if name == 'triad':
        T = [tri(['hS00'] * 3, [0] * 3, TRI_OFFS), tri(['hS04'] * 3, [0] * 3, [(0, 0), (32, 16), (-24, 40)]),
             tri(['hS00', 'hS02', 'hS04'], [0] * 3, TRI_OFFS), tri(['hSR1', 'hS00', 'hS00'], [0] * 3, TRI_OFFS),
             tri(['hS00', 'hS00', 'hSD0'], [0] * 3, TRI_OFFS), tri(['hS00'] * 3, [0, 0, 1], TRI_OFFS),
             tri(['hS00'] * 3, [0] * 3, [(0, 0), (8, 0), (16, 0)]), tri(['hSC2', 'hS00', 'hS00'], [0] * 3, TRI_OFFS),
             tri(['hS05', 'hS00', 'hS00'], [0] * 3, TRI_OFFS)]
        O = [tri(['hS00'] * 2, [0] * 2, [(0, 0), (0, 0)]), tri(['hS00'] * 3, [0] * 3, [(0, 0), (0, 0), (9.6, 3.2)]),
             tri(['hS00'] * 3, [0] * 3, [(0, 0)] * 3), tri(['hS05', 'hS00'], [0, 0], [(0, 0), (0, 0)]),
             tri(['hS04'] * 2, [0] * 2, [(0, 0), (0, 0)]), tri(['hS00', 'hSD0'], [0, 0], [(0, 0), (0, 0)]),
             tri(['hS00', 'hSR1'], [0, 0], [(0, 0), (0, 0)]), tri(['hS05'] * 2, [0] * 2, [(0, 0), (0, 0)]),
             tri(['hS00'] * 2, [0] * 2, [(16, 16), (16, 16)])]
        return [(T, 140), (O, 160)], []
    if name == 'crowd':
        codes = ['hS00', 'hS00', 'hSC1', 'hSC2', 'hSR1', 'hSR2', 'hS02', 'hS04', 'hSD0', 'hSC1']
        owners = [0, 1, 0, 0, 0, 0, 0, 1, 0, 1]
        offs = [(i % 4 * 12.0, i // 4 * 12.0) for i in range(10)]
        acts = [(120, '\n '.join('call IssuePointOrder({%d},"move",1008.0,1040.0)' % i for i in range(10)))]
        crowd = dict(units=[(c, o, dx, dy) for c, o, (dx, dy) in zip(codes, owners, offs)], actions=acts, center=(1094.4, 1008.0))
        blocked = [(x, y) for x in range(30, 34) for y in range(20, 45)]
        return [([crowd], 300)], blocked
    if name == 'ground':
        def group(code, y, first, second):
            return dict(units=[(code, 0, i * 40.0, 0.0) for i in range(4)], center=(256.0, y),
                        actions=[(20, '\n '.join('call IssuePointOrder({%d},"move",%.1f,%.1f)' % ((i,) + first) for i in range(4))),
                                 (200, '\n '.join('call IssuePointOrder({%d},"move",%.1f,%.1f)' % ((i,) + second) for i in range(4)))])
        en = group('hSC1', 1552.0, (720.0, 1552.0), (1552.0, 1616.0))
        dis = group('hSD1', 400.0, (720.0, 400.0), (1552.0, 464.0))
        rings = [(x, y) for y0 in (44, 8) for x in range(18, 27) for y in range(y0, y0 + 9) if x in (18, 26) or y in (y0, y0 + 8)]
        wall = [(x, y) for x in (36, 37) for y in range(64) if y not in (14, 15, 50, 51)]
        return [([en, dis], 400)], sorted(set(rings + wall))
    raise ValueError(name)


def jass(name):
    phases, blocked = variant_phases(name)
    lines, step, tick, idx = [], [], 2, 0
    roster = []
    for phase_index, (clusters, length) in enumerate(phases):
        first = idx
        create, acts = [], {}
        for ci, cl in enumerate(clusters):
            cx, cy = cl.get('center') or centers()[ci]
            refs = []
            for code, owner, dx, dy in cl['units']:
                create.append(" call SepMake(%d,%d,'%s',%.4f,%.4f)" % (idx, owner, code, cx + dx, cy + dy))
                roster.append(dict(i=idx, code=code, owner=owner, x=cx + dx, y=cy + dy, cluster=ci, phase=phase_index, created=tick))
                refs.append('udg_SepU[%d]' % idx); idx += 1
            for dt, stmt in cl['actions']:
                acts.setdefault(tick + dt, []).append(' ' + stmt.format(*refs) + '\n call Preload("PATHSEP tick="+I2S(udg_SepTick)+" label=action cluster=%d")' % ci)
        last = idx - 1
        step.append(' if udg_SepTick==%d then\n call Preload("PATHSEP tick=%d label=phase first=%d last=%d")\n%s\n endif' % (tick, tick, first, last, '\n'.join(create)))
        for t in sorted(acts):
            step.append(' if udg_SepTick==%d then\n%s\n endif' % (t, '\n'.join(acts[t])))
        end = tick + length
        step.append(' if udg_SepTick>%d and udg_SepTick<=%d and (udg_SepTick/2)*2==udg_SepTick then\n set i=%d\n loop\n exitwhen i>%d\n call SepRec(i)\n set i=i+1\n endloop\n endif' % (tick, end, first, last))
        step.append(' if udg_SepTick==%d then\n set i=%d\n loop\n exitwhen i>%d\n call RemoveUnit(udg_SepU[i])\n set i=i+1\n endloop\n call Preload("PATHSEP tick=%d label=phase_end")\n endif' % (end + 1, first, last, end + 1))
        tick = end + 10
    final = tick
    step.append(' if udg_SepTick==%d then\n call Preload("PATHSEP tick=%d label=complete")\n call PreloadGenEnd("sepres-%s.txt")\n call DestroyTimer(GetExpiredTimer())\n endif' % (final, final, name))
    probe = '''globals
 unit array udg_SepU
 integer udg_SepTick=0
endglobals
function SepRec takes integer i returns nothing
 local unit u=udg_SepU[i]
 call Preload("PATHSEP tick="+I2S(udg_SepTick)+" label=s i="+I2S(i)+" x="+R2SW(GetUnitX(u),1,4)+" y="+R2SW(GetUnitY(u),1,4)+" o="+I2S(GetUnitCurrentOrder(u))+" p="+I2S(GetPlayerId(GetOwningPlayer(u))))
 set u=null
endfunction
function SepMake takes integer i,integer p,integer rc,real x,real y returns nothing
 set udg_SepU[i]=CreateUnit(Player(p),rc,x,y,0.0)
 call SetUnitX(udg_SepU[i],x)
 call SetUnitY(udg_SepU[i],y)
 call SetUnitAcquireRange(udg_SepU[i],0.0)
 call Preload("PATHSEP tick="+I2S(udg_SepTick)+" label=unit i="+I2S(i)+" code="+I2S(rc)+" p="+I2S(p)+" h="+I2S(GetHandleId(udg_SepU[i]))+" x="+R2SW(GetUnitX(udg_SepU[i]),1,4)+" y="+R2SW(GetUnitY(udg_SepU[i]),1,4))
endfunction
function SepStep takes nothing returns nothing
 local integer i
 set udg_SepTick=udg_SepTick+1
@STEP@
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("PATHSEP tick=0 label=start_@NAME@")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function SepStep)
endfunction
'''.replace('@STEP@', '\n'.join(step)).replace('@NAME@', name)
    return probe, blocked, roster, final


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--variant', choices=('policy', 'triad', 'crowd', 'ground', 'immobile'), required=True)
    ap.add_argument('--name', required=True, help='RS-<TASK>-<variant>, installed as Maps/<name>.w3m')
    ap.add_argument('--tool', type=Path, required=True, help='mpqtool binary')
    ap.add_argument('--keep', type=Path, required=True, help='directory for build artifacts (j/w3u/w3a/json)')
    a = ap.parse_args()
    if a.variant == 'immobile':
        UNITS.update(hSI0=dict(urpo=1, umvs=0), hSF0=dict(urpo=1, umvs=0, umvt='fly', umvh=60.0))
    if not a.name.startswith('RS-SEP-'):
        ap.error('name must be RS-SEP-...')
    base = a.data / 'Maps/PathingRE-MovementBypasses86b-261003.w3m'
    out = a.data / 'Maps' / (a.name + '.w3m')
    if out.exists():
        ap.error('refusing to overwrite ' + str(out))
    raw_base = base.read_bytes()
    if hashlib.sha256(raw_base).hexdigest() != BASE_SHA:
        ap.error('base map hash differs')
    tool = str(a.tool)
    probe, blocked, roster, final = jass(a.variant)
    members = subprocess.check_output([tool, '-mpq', str(base), 'ls'], text=True).splitlines()
    a.keep.mkdir(parents=True, exist_ok=True)
    proof = {}
    with tempfile.TemporaryDirectory() as td:
        td = Path(td); cmd = [tool, '-mpq', str(td / 'payload.mpq'), 'pack']
        for i, m in enumerate(members):
            if m == '(listfile)':
                continue
            raw = subprocess.check_output([tool, '-mpq', str(base), 'cat', m]); orig = hashlib.sha256(raw).hexdigest()
            if m == 'war3map.j':
                s = raw.decode().replace('\r\n', '\n')
                st, en = s.index(' unit udg_PathProbeUnit=null'), s.index('function InitGlobals takes')
                s = s[:st] + probe.removeprefix('globals\n') + '\n' + s[en:]
                assert s.count('call PathProbeInit()') == 1
                raw = s.encode(); (a.keep / (a.name + '.j')).write_bytes(raw)
            if m == 'war3map.shd':
                raw = bytes(4096)
            if m == 'war3map.wpm':
                raw = wpm(blocked)
            if m == 'war3map.w3u':
                raw = w3u_rows(raw); (a.keep / (a.name + '.w3u')).write_bytes(raw)
            p = td / str(i); p.write_bytes(raw); cmd += [str(p), m]
            proof[m] = dict(original_sha256=orig, sha256=hashlib.sha256(raw).hexdigest())
        ab = w3a_channel(); (td / 'w3a').write_bytes(ab); (a.keep / (a.name + '.w3a')).write_bytes(ab)
        cmd += [str(td / 'w3a'), 'war3map.w3a']; proof['war3map.w3a'] = dict(added=True, sha256=hashlib.sha256(ab).hexdigest())
        subprocess.run(cmd, check=True)
        out.write_bytes(raw_base[:512] + (td / 'payload.mpq').read_bytes())
    meta = dict(name=a.name, variant=a.variant, base=str(base), base_sha256=BASE_SHA, sha256=hashlib.sha256(out.read_bytes()).hexdigest(),
                builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), tool_sha256=hashlib.sha256(a.tool.read_bytes()).hexdigest(),
                members=proof, blocked_cells=blocked, roster=roster, final_tick=final, units=UNITS, preload='sepres-%s.txt' % a.variant)
    (a.keep / (a.name + '.json')).write_text(json.dumps(meta, indent=1) + '\n')
    print(json.dumps({k: meta[k] for k in ('name', 'sha256', 'final_tick')}))


if __name__ == '__main__':
    main()
