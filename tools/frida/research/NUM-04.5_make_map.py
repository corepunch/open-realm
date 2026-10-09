#!/usr/bin/env python3
"""Build RS-NUM-04.5 startup random-state probe maps from the flat 64x64 passage base used by the SEP research maps.

Base: <research data>/Maps/PathingRE-MovementBypasses86b-261003.w3m (sha256 4b9ea0ba...6555). Changes: war3map.j probe
region and the config() race preferences of players 4..11 (variant), war3map.wpm (open), war3map.shd (zeroed),
war3map.w3u (SEP custom rows, reused from sep_research_map), added war3map.w3a (unused channel clone, as SEP maps).
All observations are public JASS: GetPlayerRace, GetRandomInt/Real, ChooseRandomCreep/Item/NPBuilding, unit
life after an attack exchange, unit positions after an exact overlap and a Move, SetRandomSeed. Output: Preload file
numrng-<variant>.txt. Never overwrites an existing map.
"""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sep_research_map as sep  # noqa: E402  (w3u_rows, w3a_channel, wpm, BASE_SHA)

PREFS = {  # race preference for players 4..11 (players 0..3 keep the base RACE_PREF_HUMAN rows)
    'mixed': ['RACE_PREF_ORC', 'RACE_PREF_RANDOM', 'RACE_PREF_UNDEAD', 'RACE_PREF_RANDOM',
              'RACE_PREF_NIGHTELF', 'RACE_PREF_RANDOM', 'RACE_PREF_HUMAN', 'RACE_PREF_RANDOM'],
    'fixed': ['RACE_PREF_HUMAN'] * 8,
    'default': None,  # leave players 4..11 untouched (same as the triad family)
}

PROBE = r'''globals
 unit array udg_NumU
 integer udg_NumTick=0
endglobals
function NumLog takes string s returns nothing
 call Preload("NUMRNG tick="+I2S(udg_NumTick)+" "+s)
endfunction
function NumRec takes integer i returns nothing
 local unit u=udg_NumU[i]
 call NumLog("label=s i="+I2S(i)+" x="+R2SW(GetUnitX(u),1,4)+" y="+R2SW(GetUnitY(u),1,4)+" life="+R2SW(GetWidgetLife(u),1,4)+" o="+I2S(GetUnitCurrentOrder(u)))
 set u=null
endfunction
function NumMake takes integer i,integer p,integer rc,real x,real y returns nothing
 set udg_NumU[i]=CreateUnit(Player(p),rc,x,y,0.0)
 call SetUnitX(udg_NumU[i],x)
 call SetUnitY(udg_NumU[i],y)
 call SetUnitAcquireRange(udg_NumU[i],0.0)
 call NumLog("label=unit i="+I2S(i)+" code="+I2S(rc)+" p="+I2S(p)+" h="+I2S(GetHandleId(udg_NumU[i])))
endfunction
function NumRaceId takes race r returns integer
 if r==RACE_HUMAN then
  return 1
 elseif r==RACE_ORC then
  return 2
 elseif r==RACE_UNDEAD then
  return 3
 elseif r==RACE_NIGHTELF then
  return 4
 elseif r==RACE_DEMON then
  return 5
 elseif r==RACE_OTHER then
  return 7
 endif
 return 0
endfunction
function NumQueries takes string tag returns nothing
 call NumLog("label=query tag="+tag+" int="+I2S(GetRandomInt(0,2147483647))+" int2="+I2S(GetRandomInt(-1000,1000))+" real="+R2SW(GetRandomReal(0.0,1.0),1,7))
endfunction
function NumStreams takes string tag returns nothing
 call NumLog("label=streams tag="+tag+" creep="+I2S(ChooseRandomCreep(1))+" item="+I2S(ChooseRandomItem(1))+" itemex="+I2S(ChooseRandomItemEx(ITEM_TYPE_PERMANENT,2))+" npb="+I2S(ChooseRandomNPBuilding()))
endfunction
function NumStep takes nothing returns nothing
 local integer i
 set udg_NumTick=udg_NumTick+1
 if udg_NumTick==1 then
  call NumStreams("t1")
 endif
 if udg_NumTick==2 then
  call NumMake(0,0,'hS00',1024.0,1024.0)
  call NumMake(1,0,'hS00',1024.0,1024.0)
  call NumMake(2,0,'hS00',1040.0,1040.0)
  call NumMake(3,0,'hfoo',400.0,400.0)
  call NumMake(4,12,'hfoo',440.0,400.0)
  call NumMake(5,0,'hS00',1600.0,400.0)
 endif
 if udg_NumTick==4 then
  call IssueTargetOrder(udg_NumU[3],"attack",udg_NumU[4])
  call IssueTargetOrder(udg_NumU[4],"attack",udg_NumU[3])
  call IssuePointOrder(udg_NumU[5],"move",1600.0,1600.0)
  call NumLog("label=orders")
 endif
 if udg_NumTick>2 and udg_NumTick<=80 then
  set i=0
  loop
   exitwhen i>5
   call NumRec(i)
   set i=i+1
  endloop
 endif
 if udg_NumTick==81 then
  call NumQueries("t81")
  call NumStreams("t81")
  call SetRandomSeed(12345)
  call NumLog("label=reseed seed=12345")
  call NumQueries("t81r")
  call NumStreams("t81r")
 endif
 if udg_NumTick==82 then
  call NumLog("label=complete")
  call PreloadGenEnd("numrng-@NAME@.txt")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer p=0
 call PreloadGenClear()
 call PreloadGenStart()
 call NumLog("label=start_@NAME@")
 loop
  exitwhen p>11
  call NumLog("label=race p="+I2S(p)+" race="+I2S(NumRaceId(GetPlayerRace(Player(p)))))
  set p=p+1
 endloop
 call NumQueries("init")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function NumStep)
endfunction
'''


def config_patch(s, variant):
    prefs = PREFS[variant]
    if prefs is None:
        return s
    anchor = '    call SetPlayerController( Player(3), MAP_CONTROL_USER )\n'
    if s.count(anchor) != 1:
        raise ValueError('player 3 controller anchor differs')
    extra = ''.join('    call SetPlayerRacePreference( Player(%d), %s )\n' % (4 + i, p) for i, p in enumerate(prefs))
    return s.replace(anchor, anchor + extra)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--variant', choices=sorted(PREFS), required=True)
    ap.add_argument('--name', required=True, help='RS-NUM-04.5-<variant>, installed with _env/install-map.sh')
    ap.add_argument('--tool', type=Path, required=True, help='mpqtool binary')
    ap.add_argument('--out', type=Path, required=True, help='directory for the built map and artifacts (j/json)')
    a = ap.parse_args()
    if not a.name.startswith('RS-NUM-04.5-'):
        ap.error('name must be RS-NUM-04.5-...')
    base = a.data / 'Maps/PathingRE-MovementBypasses86b-261003.w3m'
    out = a.out / (a.name + '.w3m')
    if out.exists():
        ap.error('refusing to overwrite ' + str(out))
    raw_base = base.read_bytes()
    if hashlib.sha256(raw_base).hexdigest() != sep.BASE_SHA:
        ap.error('base map hash differs')
    tool = str(a.tool)
    probe = PROBE.replace('@NAME@', a.variant)
    members = subprocess.check_output([tool, '-mpq', str(base), 'ls'], text=True).splitlines()
    a.out.mkdir(parents=True, exist_ok=True)
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
                s = config_patch(s, a.variant)
                raw = s.encode(); (a.out / (a.name + '.j')).write_bytes(raw)
            if m == 'war3map.shd':
                raw = bytes(4096)
            if m == 'war3map.wpm':
                raw = sep.wpm([])
            if m == 'war3map.w3u':
                raw = sep.w3u_rows(raw)
            p = td / str(i); p.write_bytes(raw); cmd += [str(p), m]
            proof[m] = dict(original_sha256=orig, sha256=hashlib.sha256(raw).hexdigest())
        ab = sep.w3a_channel(); (td / 'w3a').write_bytes(ab)
        cmd += [str(td / 'w3a'), 'war3map.w3a']; proof['war3map.w3a'] = dict(added=True, sha256=hashlib.sha256(ab).hexdigest())
        subprocess.run(cmd, check=True)
        out.write_bytes(raw_base[:512] + (td / 'payload.mpq').read_bytes())
    meta = dict(name=a.name, variant=a.variant, prefs_4_11=PREFS[a.variant], base=str(base), base_sha256=sep.BASE_SHA,
                sha256=hashlib.sha256(out.read_bytes()).hexdigest(), builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                sep_builder_sha256=hashlib.sha256(Path(sep.__file__).read_bytes()).hexdigest(),
                tool_sha256=hashlib.sha256(a.tool.read_bytes()).hexdigest(), members=proof, preload='numrng-%s.txt' % a.variant)
    (a.out / (a.name + '.json')).write_text(json.dumps(meta, indent=1) + '\n')
    print(json.dumps({k: meta[k] for k in ('name', 'sha256', 'preload')}))


if __name__ == '__main__':
    main()
