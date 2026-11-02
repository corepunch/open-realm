// ORDER-03.1 / ORDER-03.2 research probe (public JASS producers only; no memory writes).
// Injected into a COPY of Human02Interlude by order03_make_map.py through the unchanged
// make_wc3_pathfinding_map.instrument(). Scenario 1 registers subscribers in forward order,
// scenario 2 in reverse order (T4,T3,P1,T2,T1); every other phase is identical.
// Each trigger action writes "RSO3" Preload markers before and after its mutation, so the
// observer-free Preload file is the control for delivery order.
globals
    unit udg_O3U = null
    unit udg_O3N = null
    unit udg_O3R = null
    unit udg_O3K = null
    unit udg_O3L = null
    unit udg_O3G = null
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
    local integer i = 0
    call O3Mark("enter trig=" + n + O3Response())
    if udg_O3Phase == 2 then
        if n == "T1" then
            call O3Mark("op register begin trig=T5")
            call ExecuteFunc("O3RegisterT5")
            call O3Mark("op register end trig=T5")
            call O3Destroy("T3")
        elseif n == "T2" then
            call O3Destroy("T2")
        elseif n == "T4" then
            call O3Destroy("T1")
        endif
    elseif udg_O3Phase == 4 then
        if n == "N1" and udg_O3Nest == 0 then
            call O3Mark("op register begin trig=N4")
            call ExecuteFunc("O3RegisterN4")
            call O3Mark("op register end trig=N4")
            set udg_O3Nest = 1
            call O3Mark("op nested begin")
            call IssuePointOrder(udg_O3N, "move", -1800.0, -600.0)
            call O3Mark("op nested end")
            set udg_O3Nest = 0
        elseif n == "N1" and udg_O3Nest == 1 then
            call O3Destroy("N3")
        endif
    elseif udg_O3Phase == 6 then
        if n == "R1" then
            call O3Mark("op stop begin")
            call IssueImmediateOrder(udg_O3R, "stop")
            call O3Mark("op stop end")
        endif
    elseif udg_O3Phase == 7 then
        if n == "K1" then
            call O3Mark("op remove begin")
            call RemoveUnit(udg_O3K)
            call O3Mark("op remove end")
        endif
    elseif udg_O3Phase == 8 then
        if n == "L1" then
            call O3Mark("op kill begin")
            call KillUnit(udg_O3L)
            call O3Mark("op kill end")
        endif
    elseif udg_O3Phase == 10 then
        if n == "G1" then
            call O3Mark("op grow begin")
            call ExecuteFunc("O3RegisterGX")
            call O3Mark("op grow end")
        endif
    endif
    call O3Mark("exit trig=" + n + O3Response())
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

function O3RegisterT5 takes nothing returns nothing
    call O3New("T5", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
endfunction

function O3RegisterN4 takes nothing returns nothing
    call O3New("N4", udg_O3N, EVENT_UNIT_ISSUED_POINT_ORDER)
endfunction

// Twenty-one further distinct unit events on one trigger, registered while the unit's
// point-order dispatch is active (deferred subscriber-table growth).
function O3RegisterGX takes nothing returns nothing
    local trigger t = CreateTrigger()
    local integer e = 54
    set udg_O3T[udg_O3Count] = t
    set udg_O3Name[udg_O3Count] = "GX"
    set udg_O3Count = udg_O3Count + 1
    call TriggerAddAction(t, function O3Action)
    loop
        exitwhen e > 74
        call O3Mark("reg begin trig=GX ev=" + I2S(e))
        call TriggerRegisterUnitEvent(t, udg_O3G, ConvertUnitEvent(e))
        set e = e + 1
    endloop
    call O3Mark("reg end trig=GX")
endfunction

function O3Order takes unit u, real x, real y returns nothing
    call O3Mark("issue begin unit=" + O3UnitText(u))
    call IssuePointOrder(u, "move", x, y)
    call O3Mark("issue end unit=" + O3UnitText(u))
endfunction

function O3Tick takes nothing returns nothing
    set udg_O3Tick = udg_O3Tick + 1
    if udg_O3Tick == 5 then
        set udg_O3Phase = 0
        set udg_O3U = CreateUnit(Player(0), 'hfoo', -1936.0, -976.0, 90.0)
        call O3Mark("created U=" + O3UnitText(udg_O3U))
        if O3_SCENARIO == 1 then
            call O3New("T1", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3New("T2", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3NewPlayer("P1")
            call O3New("T3", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3New("T4", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
        else
            call O3New("T4", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3New("T3", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3NewPlayer("P1")
            call O3New("T2", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
            call O3New("T1", udg_O3U, EVENT_UNIT_ISSUED_POINT_ORDER)
        endif
    elseif udg_O3Tick == 10 then
        set udg_O3Phase = 1
        call O3Order(udg_O3U, -1936.0, -700.0)
    elseif udg_O3Tick == 15 then
        set udg_O3Phase = 2
        call O3Order(udg_O3U, -1936.0, -800.0)
    elseif udg_O3Tick == 20 then
        set udg_O3Phase = 3
        call O3Order(udg_O3U, -1936.0, -900.0)
    elseif udg_O3Tick == 25 then
        set udg_O3Phase = 4
        set udg_O3N = CreateUnit(Player(0), 'hfoo', -1800.0, -976.0, 90.0)
        call O3Mark("created N=" + O3UnitText(udg_O3N))
        call O3New("N1", udg_O3N, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("N2", udg_O3N, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("N3", udg_O3N, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3N, -1800.0, -700.0)
    elseif udg_O3Tick == 28 then
        set udg_O3Phase = 5
        call O3Order(udg_O3N, -1800.0, -800.0)
    elseif udg_O3Tick == 32 then
        set udg_O3Phase = 6
        set udg_O3R = CreateUnit(Player(0), 'hfoo', -1700.0, -976.0, 90.0)
        call O3Mark("created R=" + O3UnitText(udg_O3R))
        call O3New("R1", udg_O3R, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("R2", udg_O3R, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("RI", udg_O3R, EVENT_UNIT_ISSUED_ORDER)
        call O3Order(udg_O3R, -1700.0, -700.0)
        call O3Mark("after R=" + O3UnitText(udg_O3R))
    elseif udg_O3Tick == 36 then
        set udg_O3Phase = 7
        set udg_O3K = CreateUnit(Player(0), 'hfoo', -1600.0, -976.0, 90.0)
        call O3Mark("created K=" + O3UnitText(udg_O3K))
        call O3New("K1", udg_O3K, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("K2", udg_O3K, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("KD", udg_O3K, EVENT_UNIT_DEATH)
        call O3New("K3", udg_O3K, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3K, -1600.0, -700.0)
        call O3Mark("after K=" + O3UnitText(udg_O3K))
    elseif udg_O3Tick == 37 then
        call O3Mark("next K=" + O3UnitText(udg_O3K))
    elseif udg_O3Tick == 40 then
        set udg_O3Phase = 8
        set udg_O3L = CreateUnit(Player(0), 'hfoo', -1500.0, -976.0, 90.0)
        call O3Mark("created L=" + O3UnitText(udg_O3L))
        call O3New("L1", udg_O3L, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("L2", udg_O3L, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("LD", udg_O3L, EVENT_UNIT_DEATH)
        call O3New("L3", udg_O3L, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3L, -1500.0, -700.0)
        call O3Mark("after L=" + O3UnitText(udg_O3L))
    elseif udg_O3Tick == 45 then
        set udg_O3Phase = 9
        call O3Mark("remove begin L=" + O3UnitText(udg_O3L))
        call RemoveUnit(udg_O3L)
        call O3Mark("remove end L=" + O3UnitText(udg_O3L))
    elseif udg_O3Tick == 50 then
        set udg_O3Phase = 10
        set udg_O3G = CreateUnit(Player(0), 'hfoo', -1400.0, -976.0, 90.0)
        call O3Mark("created G=" + O3UnitText(udg_O3G))
        call O3New("G1", udg_O3G, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3New("G2", udg_O3G, EVENT_UNIT_ISSUED_POINT_ORDER)
        call O3Order(udg_O3G, -1400.0, -700.0)
    elseif udg_O3Tick == 53 then
        set udg_O3Phase = 11
        call O3Order(udg_O3G, -1400.0, -800.0)
    elseif udg_O3Tick == 60 then
        set udg_O3Phase = 12
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
