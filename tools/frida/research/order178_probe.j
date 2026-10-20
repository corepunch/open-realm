// ORDER-03.1 / ORDER-03.2 second research probe (public JASS producers only; no memory writes).
// Injected into a COPY of Human02Interlude by order03_make_map.py --probe order03b_probe.j.
// Phase 1 (tick 5, unit V): the player-unit trigger P2 registers the FIRST unit trigger V1 on V while
//   the player event is being delivered; the unit dispatch of the same order follows. (tick 7 re-issues.)
// Phase 2 (tick 10, unit W): P2 destroys W1 (W's only unit trigger) during the player delivery.
// Phase 3 (tick 15, unit M): M1 removes M, then issues a nested point order to the removed unit.
// Phase 4 (tick 20, unit X): X1 destroys X2, then a nested point order to X; X3 registered later.
// Phase 5 (tick 25, unit Y): Y1 issues a nested order whose inner Y1 issues another (depth 3), Y2 logs.
globals
    unit udg_O3V = null
    unit udg_O3W = null
    unit udg_O3M = null
    unit udg_O3X = null
    unit udg_O3Y = null
    trigger array udg_O3T
    string array udg_O3Name
    integer udg_O3Count = 0
    integer udg_O3Tick = 0
    integer udg_O3Phase = 0
    integer udg_O3Nest = 0
    timer udg_O3Timer = null
    constant integer O3_SCENARIO = @RS_SCENARIO@
    constant string O3_NAME = "@RS_NAME@"
endglobals

function O3Mark takes string label returns nothing
    call Preload("RSO3 tick=" + I2S(udg_O3Tick) + " phase=" + I2S(udg_O3Phase) + " nest=" + I2S(udg_O3Nest) + " " + label)
endfunction

function O3Find takes string name returns integer
    local integer i = 0
    loop
        exitwhen i >= udg_O3Count
        if udg_O3Name[i] == name then
            return i
        endif
        set i = i + 1
    endloop
    return -1
endfunction

function O3Index takes trigger t returns integer
    local integer i = 0
    loop
        exitwhen i >= udg_O3Count
        if udg_O3T[i] == t then
            return i
        endif
        set i = i + 1
    endloop
    return -1
endfunction

function O3UnitText takes unit u returns string
    if u == null then
        return "null"
    endif
    return I2S(GetHandleId(u)) + ":" + I2S(GetUnitTypeId(u)) + ":" + R2S(GetWidgetLife(u)) + ":" + I2S(GetUnitCurrentOrder(u))
endfunction

function O3Response takes nothing returns string
    local trigger t = GetTriggeringTrigger()
    local string s = " ev=" + I2S(GetHandleId(GetTriggerEventId())) + " ord=" + I2S(GetIssuedOrderId())
    set s = s + " px=" + R2S(GetOrderPointX()) + " py=" + R2S(GetOrderPointY())
    set s = s + " unit=" + O3UnitText(GetTriggerUnit())
    set s = s + " eval=" + I2S(GetTriggerEvalCount(t)) + " exec=" + I2S(GetTriggerExecCount(t))
    set t = null
    return s
endfunction

function O3Name takes trigger t returns string
    local integer i = O3Index(t)
    if i < 0 then
        return "?"
    endif
    return udg_O3Name[i]
endfunction

function O3Destroy takes string name returns nothing
    local integer i = O3Find(name)
    call O3Mark("op destroy begin trig=" + name)
    call DestroyTrigger(udg_O3T[i])
    call O3Mark("op destroy end trig=" + name)
endfunction

function O3Action takes nothing returns nothing
    local string n = O3Name(GetTriggeringTrigger())
    local unit u = GetTriggerUnit()
    call O3Mark("enter trig=" + n + O3Response())
    if n == "P2" and udg_O3Phase == 1 and u == udg_O3V then
        call O3Mark("op register begin trig=V1")
        call ExecuteFunc("O3RegisterV1")
        call O3Mark("op register end trig=V1")
    elseif n == "P2" and udg_O3Phase == 3 and u == udg_O3W then
        call O3Destroy("W1")
    elseif n == "M1" and udg_O3Nest == 0 then
        call O3Mark("op remove begin")
        call RemoveUnit(udg_O3M)
        call O3Mark("op remove end" + O3Response())
        set udg_O3Nest = 1
        call O3Mark("op nested begin")
        if IssuePointOrder(udg_O3M, "move", -1600.0, -600.0) then
            call O3Mark("nested native result=true")
        else
            call O3Mark("nested native result=false")
        endif
        call O3Mark("op nested end")
        set udg_O3Nest = 0
    elseif n == "X1" and udg_O3Nest == 0 then
        call O3Destroy("X2")
        set udg_O3Nest = 1
        call O3Mark("op nested begin")
        call IssuePointOrder(udg_O3X, "move", -1500.0, -600.0)
        call O3Mark("op nested end")
        set udg_O3Nest = 0
        call O3Mark("op register begin trig=X3")
        call ExecuteFunc("O3RegisterX3")
        call O3Mark("op register end trig=X3")
    elseif n == "Y1" and udg_O3Nest < 2 then
        set udg_O3Nest = udg_O3Nest + 1
        call O3Mark("op nested begin")
        call IssuePointOrder(udg_O3Y, "move", -1400.0, -600.0 - 50.0 * udg_O3Nest)
        call O3Mark("op nested end")
        set udg_O3Nest = udg_O3Nest - 1
    endif
    call O3Mark("exit trig=" + n + O3Response())
    set u = null
endfunction

function O3New takes string name, unit u, unitevent e returns trigger
    local trigger t = CreateTrigger()
    set udg_O3T[udg_O3Count] = t
    set udg_O3Name[udg_O3Count] = name
    set udg_O3Count = udg_O3Count + 1
    call O3Mark("reg begin trig=" + name + " ev=" + I2S(GetHandleId(e)))
    call TriggerRegisterUnitEvent(t, u, e)
    call TriggerAddAction(t, function O3Action)
    call O3Mark("reg end trig=" + name)
    return t
endfunction

function O3NewPlayer takes string name returns trigger
    local trigger t = CreateTrigger()
    set udg_O3T[udg_O3Count] = t
    set udg_O3Name[udg_O3Count] = name
    set udg_O3Count = udg_O3Count + 1
    call O3Mark("reg begin trig=" + name + " ev=" + I2S(GetHandleId(EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER)))
    call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)
    call TriggerAddAction(t, function O3Action)
    call O3Mark("reg end trig=" + name)
    return t
endfunction

function O3RegisterV1 takes nothing returns nothing
    call O3New("V1", udg_O3V, EVENT_UNIT_ISSUED_POINT_ORDER)
endfunction

function O3RegisterX3 takes nothing returns nothing
    call O3New("X3", udg_O3X, EVENT_UNIT_ISSUED_POINT_ORDER)
endfunction

function O3Order takes unit u, real x, real y returns nothing
    call O3Mark("issue begin unit=" + O3UnitText(u))
    call IssuePointOrder(u, "move", x, y)
    call O3Mark("issue end unit=" + O3UnitText(u))
endfunction

function O3Tick takes nothing returns nothing
    set udg_O3Tick = udg_O3Tick + 1
    if udg_O3Tick == 3 then
        call O3NewPlayer("P2")
    elseif udg_O3Tick == 5 then
        set udg_O3Phase = 1
        set udg_O3V = CreateUnit(Player(0), 'hfoo', -1936.0, -976.0, 90.0)
        call O3Mark("created V=" + O3UnitText(udg_O3V))
        call O3Order(udg_O3V, -1936.0, -700.0)
    elseif udg_O3Tick == 7 then
        set udg_O3Phase = 2
        call O3Order(udg_O3V, -1936.0, -800.0)
    elseif udg_O3Tick == 10 then
        set udg_O3Phase = 3
        set udg_O3W = CreateUnit(Player(0), 'hfoo', -1800.0, -976.0, 90.0)
        call O3Mark("created W=" + O3UnitText(udg_O3W))
        call O3New("W1", udg_O3W, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3W, -1800.0, -700.0)
    elseif udg_O3Tick == 12 then
        set udg_O3Phase = 4
        call O3Order(udg_O3W, -1800.0, -800.0)
    elseif udg_O3Tick == 15 then
        set udg_O3Phase = 5
        set udg_O3M = CreateUnit(Player(0), 'hfoo', -1600.0, -976.0, 90.0)
        call O3Mark("created M=" + O3UnitText(udg_O3M))
        call O3New("M1", udg_O3M, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("M2", udg_O3M, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3M, -1600.0, -700.0)
    elseif udg_O3Tick == 16 then
        call O3Mark("next M=" + O3UnitText(udg_O3M))
    elseif udg_O3Tick == 20 then
        set udg_O3Phase = 6
        set udg_O3X = CreateUnit(Player(0), 'hfoo', -1500.0, -976.0, 90.0)
        call O3Mark("created X=" + O3UnitText(udg_O3X))
        call O3New("X1", udg_O3X, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("X2", udg_O3X, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3X, -1500.0, -700.0)
    elseif udg_O3Tick == 22 then
        set udg_O3Phase = 7
        call O3Order(udg_O3X, -1500.0, -800.0)
    elseif udg_O3Tick == 25 then
        set udg_O3Phase = 8
        set udg_O3Y = CreateUnit(Player(0), 'hfoo', -1400.0, -976.0, 90.0)
        call O3Mark("created Y=" + O3UnitText(udg_O3Y))
        call O3New("Y1", udg_O3Y, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("Y2", udg_O3Y, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3Y, -1400.0, -700.0)
    elseif udg_O3Tick == 35 then
        set udg_O3Phase = 9
        call O3Mark("complete")
        call PreloadGenEnd("rs-o3-" + O3_NAME + ".txt")
        call PauseTimer(udg_O3Timer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    call PreloadGenClear()
    call PreloadGenStart()
    call Preload("RSO3 tick=0 phase=0 nest=0 init scenario=" + I2S(O3_SCENARIO))
    set udg_O3Timer = CreateTimer()
    call TimerStart(udg_O3Timer, 0.10, true, function O3Tick)
endfunction
