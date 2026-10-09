// ORDER-05.1 / 05.2 / 05.3 research probe (public JASS producers only; no memory writes).
// Injected into a COPY of Human02Interlude by order05_make_map.py.
// Request-clock producers used: DestroyTrigger (agent release request, min delay) and
// TriggerRegisterUnitInRange (range listener, repeating 1/8 s request). The probe clock is a JASS
// timer (timer heap, not the request clock); "el" is the elapsed time of a 1000 s one-shot JASS timer.
// Scenario 1 "requests":
//   tick 5   D1..D4 created; DestroyTrigger D3,D1,D4,D2 then D3 again in one callback (equal deadlines, duplicate)
//   tick 6   N1..N4 created (handle reuse order after the releases)
//   tick 10  RA, RB registered in one callback (equal deadlines); tick 11 RC (different phase)
//   tick 15  U moved into range of V -> RA/RB/RC enter events; tick 18 U moved away
//   tick 25  arm: RA's next action destroys RB and registers RD (inside the request callback)
//   tick 28  U enters; tick 31 U leaves
//   tick 34  arm: RC's next action destroys RC itself; tick 36 U enters; tick 39 U leaves; tick 45 complete
// Scenario 2 "saveload" / 3 "wrap": RA, RB registered at tick 10; U enters every 100 ticks (tick%100==50)
//   and leaves 30 ticks later; markers every 10 ticks. Ends at tick 1000 (saveload) / 3400 (wrap).
globals
    unit udg_O5U = null
    unit udg_O5V = null
    trigger array udg_O5T
    string array udg_O5Name
    integer udg_O5Count = 0
    integer udg_O5Tick = 0
    integer udg_O5Phase = 0
    integer udg_O5Arm = 0
    timer udg_O5Timer = null
    timer udg_O5Clock = null
    constant integer O5_SCENARIO = @RS_SCENARIO@
    constant string O5_NAME = "@RS_NAME@"
endglobals

function O5Mark takes string label returns nothing
    call Preload("RSO5 tick=" + I2S(udg_O5Tick) + " phase=" + I2S(udg_O5Phase) + " el=" + R2SW(TimerGetElapsed(udg_O5Clock), 1, 3) + " " + label)
endfunction

function O5Find takes string name returns integer
    local integer i = 0
    loop
        exitwhen i >= udg_O5Count
        if udg_O5Name[i] == name then
            return i
        endif
        set i = i + 1
    endloop
    return -1
endfunction

function O5Index takes trigger t returns integer
    local integer i = 0
    loop
        exitwhen i >= udg_O5Count
        if udg_O5T[i] == t then
            return i
        endif
        set i = i + 1
    endloop
    return -1
endfunction

function O5Name takes trigger t returns string
    local integer i = O5Index(t)
    if i < 0 then
        return "?"
    endif
    return udg_O5Name[i]
endfunction

function O5Destroy takes string name returns nothing
    local integer i = O5Find(name)
    call O5Mark("op destroy begin trig=" + name + " h=" + I2S(GetHandleId(udg_O5T[i])))
    call DestroyTrigger(udg_O5T[i])
    call O5Mark("op destroy end trig=" + name)
endfunction

function O5Plain takes string name returns trigger
    local trigger t = CreateTrigger()
    set udg_O5T[udg_O5Count] = t
    set udg_O5Name[udg_O5Count] = name
    set udg_O5Count = udg_O5Count + 1
    call O5Mark("create trig=" + name + " h=" + I2S(GetHandleId(t)))
    return t
endfunction

function O5Action takes nothing returns nothing
    local trigger t = GetTriggeringTrigger()
    local string n = O5Name(t)
    call O5Mark("enter trig=" + n + " h=" + I2S(GetHandleId(t)) + " unit=" + I2S(GetHandleId(GetTriggerUnit())) + " eval=" + I2S(GetTriggerEvalCount(t)) + " exec=" + I2S(GetTriggerExecCount(t)))
    if n == "RA" and udg_O5Arm == 1 then
        set udg_O5Arm = 0
        call O5Destroy("RB")
        call O5Mark("op range begin trig=RD")
        call TriggerRegisterUnitInRange(O5Plain("RD"), udg_O5V, 150.0, null)
        call TriggerAddAction(udg_O5T[O5Find("RD")], function O5Action)
        call O5Mark("op range end trig=RD")
    elseif n == "RC" and udg_O5Arm == 2 then
        set udg_O5Arm = 0
        call O5Destroy("RC")
    endif
    call O5Mark("exit trig=" + n)
    set t = null
endfunction

function O5Range takes string name returns nothing
    local trigger t = O5Plain(name)
    call O5Mark("reg begin trig=" + name)
    call TriggerRegisterUnitInRange(t, udg_O5V, 150.0, null)
    call TriggerAddAction(t, function O5Action)
    call O5Mark("reg end trig=" + name)
    set t = null
endfunction

function O5Move takes real x, real y returns nothing
    call O5Mark("move begin x=" + R2S(x))
    call SetUnitPosition(udg_O5U, x, y)
    call O5Mark("move end x=" + R2S(GetUnitX(udg_O5U)) + " y=" + R2S(GetUnitY(udg_O5U)))
endfunction

function O5Requests takes nothing returns nothing
    local integer t = udg_O5Tick
    if t == 5 then
        set udg_O5Phase = 1
        call O5Mark("win=1")
        call O5Plain("D1")
        call O5Plain("D2")
        call O5Plain("D3")
        call O5Plain("D4")
        call O5Destroy("D3")
        call O5Destroy("D1")
        call O5Destroy("D4")
        call O5Destroy("D2")
        call O5Destroy("D3")
    elseif t == 6 then
        call O5Plain("N1")
        call O5Plain("N2")
        call O5Plain("N3")
        call O5Plain("N4")
    elseif t == 8 then
        call O5Mark("win=0")
    elseif t == 10 then
        set udg_O5Phase = 2
        call O5Mark("win=1")
        call O5Range("RA")
        call O5Range("RB")
    elseif t == 11 then
        call O5Range("RC")
    elseif t == 15 then
        call O5Move(-1836.0, -976.0)
    elseif t == 18 then
        call O5Move(-1336.0, -976.0)
    elseif t == 20 then
        call O5Mark("win=0")
    elseif t == 25 then
        set udg_O5Phase = 3
        call O5Mark("win=1")
        set udg_O5Arm = 1
    elseif t == 28 then
        call O5Move(-1836.0, -976.0)
    elseif t == 31 then
        call O5Move(-1336.0, -976.0)
    elseif t == 34 then
        set udg_O5Phase = 4
        set udg_O5Arm = 2
    elseif t == 36 then
        call O5Move(-1836.0, -976.0)
    elseif t == 39 then
        call O5Move(-1336.0, -976.0)
    elseif t == 42 then
        call O5Mark("win=0")
    elseif t == 45 then
        set udg_O5Phase = 9
        call O5Mark("complete")
        call PreloadGenEnd("rs-o5-" + O5_NAME + ".txt")
        call PauseTimer(udg_O5Timer)
    endif
endfunction

function O5Periodic takes nothing returns nothing
    local integer t = udg_O5Tick
    if t == 10 then
        set udg_O5Phase = 2
        call O5Mark("win=1")
        call O5Range("RA")
        call O5Range("RB")
    elseif t == 20 then
        call O5Mark("win=0")
    elseif ModuloInteger(t, 100) == 50 then
        call O5Mark("win=1")
        call O5Move(-1836.0, -976.0)
    elseif ModuloInteger(t, 100) == 80 then
        call O5Move(-1336.0, -976.0)
        call O5Mark("win=0")
    elseif ModuloInteger(t, 10) == 0 then
        call O5Mark("beat")
    endif
    if (t == 1000 and O5_SCENARIO == 2) or t == 3400 then
        set udg_O5Phase = 9
        call O5Mark("complete")
        call PreloadGenEnd("rs-o5-" + O5_NAME + ".txt")
        call PauseTimer(udg_O5Timer)
    endif
endfunction

function O5Tick takes nothing returns nothing
    set udg_O5Tick = udg_O5Tick + 1
    if udg_O5Tick == 3 then
        set udg_O5V = CreateUnit(Player(0), 'hfoo', -1936.0, -976.0, 90.0)
        set udg_O5U = CreateUnit(Player(0), 'hfoo', -1336.0, -976.0, 90.0)
        call O5Mark("created V=" + I2S(GetHandleId(udg_O5V)) + " U=" + I2S(GetHandleId(udg_O5U)))
    elseif O5_SCENARIO == 1 then
        call O5Requests()
    else
        call O5Periodic()
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    call PreloadGenClear()
    call PreloadGenStart()
    set udg_O5Clock = CreateTimer()
    call TimerStart(udg_O5Clock, 1000.0, false, null)
    call Preload("RSO5 tick=0 phase=0 el=0 init scenario=" + I2S(O5_SCENARIO))
    set udg_O5Timer = CreateTimer()
    call TimerStart(udg_O5Timer, 0.10, true, function O5Tick)
endfunction
