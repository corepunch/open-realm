// ORDER-03.2 third research probe (player-event payload lifetime) (public JASS producers only; no memory writes).
// Injected into a COPY of Human02Interlude by order03_make_map.py --probe order03b_probe.j.
// Player triggers Pa then Pb (EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER) and unit triggers on Z / Q.
// Phase 1 (tick 5, unit Z): Pa issues Stop to Z during the player delivery; Pb and Z1 then read the
//   original order payload (GetIssuedOrderId / GetOrderPointX/Y) of the same producer call.
// Phase 2 (tick 10, unit Q): Pa removes Q during the player delivery; Pb and Q1 still read the payload.
globals
    unit udg_O3Z = null
    unit udg_O3Q = null
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
    if n == "Pa" and udg_O3Phase == 1 and u == udg_O3Z then
        call O3Mark("op stop begin")
        call IssueImmediateOrder(udg_O3Z, "stop")
        call O3Mark("op stop end")
    elseif n == "Pa" and udg_O3Phase == 2 and u == udg_O3Q then
        call O3Mark("op remove begin")
        call RemoveUnit(udg_O3Q)
        call O3Mark("op remove end")
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

function O3NewPlayer takes string name, playerunitevent e returns trigger
    local trigger t = CreateTrigger()
    set udg_O3T[udg_O3Count] = t
    set udg_O3Name[udg_O3Count] = name
    set udg_O3Count = udg_O3Count + 1
    call O3Mark("reg begin trig=" + name + " ev=" + I2S(GetHandleId(e)))
    call TriggerRegisterPlayerUnitEvent(t, Player(0), e, null)
    call TriggerAddAction(t, function O3Action)
    call O3Mark("reg end trig=" + name)
    return t
endfunction

function O3Order takes unit u, real x, real y returns nothing
    call O3Mark("issue begin unit=" + O3UnitText(u))
    call IssuePointOrder(u, "move", x, y)
    call O3Mark("issue end unit=" + O3UnitText(u))
endfunction

function O3Tick takes nothing returns nothing
    set udg_O3Tick = udg_O3Tick + 1
    if udg_O3Tick == 3 then
        call O3NewPlayer("Pa", EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER)
        call O3NewPlayer("Pb", EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER)
        call O3NewPlayer("Pi", EVENT_PLAYER_UNIT_ISSUED_ORDER)
    elseif udg_O3Tick == 5 then
        set udg_O3Phase = 1
        set udg_O3Z = CreateUnit(Player(0), 'hfoo', -1936.0, -976.0, 90.0)
        call O3Mark("created Z=" + O3UnitText(udg_O3Z))
        call O3New("Z1", udg_O3Z, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3Z, -1936.0, -700.0)
        call O3Mark("after Z=" + O3UnitText(udg_O3Z))
    elseif udg_O3Tick == 10 then
        set udg_O3Phase = 2
        set udg_O3Q = CreateUnit(Player(0), 'hfoo', -1800.0, -976.0, 90.0)
        call O3Mark("created Q=" + O3UnitText(udg_O3Q))
        call O3New("Q1", udg_O3Q, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3Q, -1800.0, -700.0)
        call O3Mark("after Q=" + O3UnitText(udg_O3Q))
    elseif udg_O3Tick == 11 then
        call O3Mark("next Q=" + O3UnitText(udg_O3Q))
    elseif udg_O3Tick == 20 then
        set udg_O3Phase = 3
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
