// SEP-03.x / MAP-05.x / MAP-06.x research probe. Injected into a COPY of Human02Interlude
// by sep03_map06_make_map.py (via make_wc3_pathfinding_map.instrument, unchanged).
// Scenarios: 1 spatial_ab, 2 spatial_ba, 3 restart, 4 changelevel_a, 5 changelevel_b,
//            6 save_control, 7 save_only, 8 save_load, 9 ui_route (slow crowd; save/load through the game UI).
globals
    unit udg_RSUnit = null
    unit udg_RSUnitB = null
    unit array udg_RSCrowd
    unit array udg_RSBurst
    timer udg_RSTimer = null
    integer udg_RSTick = 0
    boolean udg_RSLoadArmed = false
    constant integer RS_SCENARIO = @RS_SCENARIO@
    constant string RS_NAME = "@RS_NAME@"
endglobals

function RSRecord takes string label returns nothing
    local string line = "RSPATIAL tick=" + I2S(udg_RSTick) + " label=" + label
    if udg_RSUnit != null then
        set line = line + " x=" + R2S(GetUnitX(udg_RSUnit)) + " y=" + R2S(GetUnitY(udg_RSUnit)) + " order=" + I2S(GetUnitCurrentOrder(udg_RSUnit))
    endif
    if udg_RSUnitB != null then
        set line = line + " bx=" + R2S(GetUnitX(udg_RSUnitB)) + " by=" + R2S(GetUnitY(udg_RSUnitB))
    endif
    call Preload(line)
endfunction

function RSCrowdRecord takes nothing returns nothing
    local integer i = 0
    local string line = "RSCROWD tick=" + I2S(udg_RSTick)
    loop
        exitwhen i == 3
        if udg_RSCrowd[i] != null then
            set line = line + " c" + I2S(i) + "=" + R2S(GetUnitX(udg_RSCrowd[i])) + "," + R2S(GetUnitY(udg_RSCrowd[i]))
        endif
        set i = i + 1
    endloop
    call Preload(line)
endfunction

function RSCreate takes string name, real x, real y returns unit
    local unit u
    call Preload("RSPATIAL tick=" + I2S(udg_RSTick) + " label=create_begin name=" + name)
    set u = CreateUnit(Player(0), 'hfoo', x, y, 90.0)
    call Preload("RSPATIAL tick=" + I2S(udg_RSTick) + " label=create_end name=" + name)
    return u
endfunction

function RSRemove takes string name, unit u returns nothing
    call Preload("RSPATIAL tick=" + I2S(udg_RSTick) + " label=remove_begin name=" + name)
    call RemoveUnit(u)
    call Preload("RSPATIAL tick=" + I2S(udg_RSTick) + " label=remove_end name=" + name)
endfunction

function RSSpatialTick takes nothing returns nothing
    local integer i = 0
    local boolean ab = RS_SCENARIO == 1
    if udg_RSTick == 5 then
        if ab then
            set udg_RSUnit = RSCreate("A", -1936.0, -976.0)
        else
            set udg_RSUnitB = RSCreate("B", -1920.0, -976.0)
        endif
    elseif udg_RSTick == 6 then
        if ab then
            set udg_RSUnitB = RSCreate("B", -1920.0, -976.0)
        else
            set udg_RSUnit = RSCreate("A", -1936.0, -976.0)
        endif
        call RSRecord("snapshot_inserted")
    elseif udg_RSTick == 20 then
        call RSRecord("before_move")
        call IssuePointOrder(udg_RSUnit, "move", -1936.0, -144.0)
        call IssuePointOrder(udg_RSUnitB, "move", -1920.0, -144.0)
    elseif udg_RSTick == 50 then
        call RSRecord("snapshot_before_remove")
        if ab then
            call RSRemove("A", udg_RSUnit)
        else
            call RSRemove("B", udg_RSUnitB)
        endif
    elseif udg_RSTick == 52 then
        if ab then
            call RSRemove("B", udg_RSUnitB)
        else
            call RSRemove("A", udg_RSUnit)
        endif
        set udg_RSUnit = null
        set udg_RSUnitB = null
        call RSRecord("snapshot_removed")
    elseif udg_RSTick == 60 then
        call Preload("RSPATIAL tick=60 label=burst_begin")
        loop
            exitwhen i == 40
            set udg_RSBurst[i] = CreateUnit(Player(0), 'hfoo', -2192.0 + I2R(ModuloInteger(i, 8)) * 64.0, -1488.0 + I2R(i / 8) * 64.0, 90.0)
            set i = i + 1
        endloop
        call Preload("RSPATIAL tick=60 label=burst_end")
    elseif udg_RSTick == 62 then
        loop
            exitwhen i == 40
            call IssuePointOrder(udg_RSBurst[i], "move", -1600.0 + I2R(ModuloInteger(i, 8)) * 48.0, -600.0 + I2R(i / 8) * 48.0)
            set i = i + 1
        endloop
        call RSRecord("burst_moved")
    elseif udg_RSTick == 90 then
        call Preload("RSPATIAL tick=90 label=burst_remove_begin")
        loop
            exitwhen i == 40
            call RemoveUnit(udg_RSBurst[i])
            set udg_RSBurst[i] = null
            set i = i + 2
        endloop
        call Preload("RSPATIAL tick=90 label=burst_remove_end")
    elseif udg_RSTick == 100 then
        call Preload("RSPATIAL tick=100 label=reuse_begin")
        loop
            exitwhen i == 40
            set udg_RSBurst[i] = CreateUnit(Player(0), 'hfoo', -2192.0 + I2R(ModuloInteger(i, 8)) * 64.0, -1488.0 + I2R(i / 8) * 64.0, 90.0)
            set i = i + 4
        endloop
        call Preload("RSPATIAL tick=100 label=reuse_end")
    endif
endfunction

function RSMoverTick takes nothing returns nothing
    if udg_RSTick == 5 then
        set udg_RSUnit = RSCreate("M", -1936.0, -976.0)
        if RS_SCENARIO >= 6 then
            set udg_RSCrowd[0] = RSCreate("C0", -1904.0, -976.0)
            set udg_RSCrowd[1] = RSCreate("C1", -1936.0, -944.0)
            set udg_RSCrowd[2] = RSCreate("C2", -1904.0, -944.0)
        endif
        if RS_SCENARIO == 9 then
            call SetUnitMoveSpeed(udg_RSUnit, 80.0)
            call SetUnitMoveSpeed(udg_RSCrowd[0], 80.0)
            call SetUnitMoveSpeed(udg_RSCrowd[1], 80.0)
            call SetUnitMoveSpeed(udg_RSCrowd[2], 80.0)
        endif
        call RSRecord("snapshot_created")
    elseif udg_RSTick == 10 then
        call RSRecord("before_move")
        call IssuePointOrder(udg_RSUnit, "move", -1936.0, -144.0)
        if RS_SCENARIO >= 6 then
            call IssuePointOrder(udg_RSCrowd[0], "move", -1904.0, -144.0)
            call IssuePointOrder(udg_RSCrowd[1], "move", -1936.0, -112.0)
            call IssuePointOrder(udg_RSCrowd[2], "move", -1904.0, -112.0)
        endif
        call RSRecord("after_move")
    elseif udg_RSTick == 20 then
        if RS_SCENARIO == 3 then
            call RSRecord("before_restart")
            call PreloadGenEnd("rs-" + RS_NAME + "-prerestart.txt")
            call RestartGame(false)
            call RSRecord("after_restart_call")
        elseif RS_SCENARIO == 4 then
            call RSRecord("before_changelevel")
            call PreloadGenEnd("rs-" + RS_NAME + "-prechange.txt")
            call ChangeLevel("Maps\\RS-MAP-06.1-changelevel_b.w3m", false)
            call RSRecord("after_changelevel_call")
        elseif RS_SCENARIO == 7 or RS_SCENARIO == 8 then
            call RSRecord("before_save")
            call SaveGame("RSMAP062main")
            call RSRecord("after_save")
        endif
    elseif udg_RSTick == 25 and RS_SCENARIO >= 7 then
        if SaveGameExists("RSMAP062main") then
            call RSRecord("main_save_exists")
        else
            call RSRecord("main_save_missing")
        endif
    elseif udg_RSTick == 30 and RS_SCENARIO == 8 then
        // Disk flag written after the main save: absent before the first load, present after it.
        if not SaveGameExists("RSMAP062flag") then
            call SaveGame("RSMAP062flag")
            set udg_RSLoadArmed = true
            call RSRecord("load_armed")
        else
            call RSRecord("resumed_after_load")
        endif
    elseif udg_RSTick == 35 and RS_SCENARIO == 8 and udg_RSLoadArmed then
        call RSRecord("before_load")
        call LoadGame("RSMAP062main", false)
        call RSRecord("after_load_call")
    endif
endfunction

function RSTick takes nothing returns nothing
    set udg_RSTick = udg_RSTick + 1
    if RS_SCENARIO <= 2 then
        call RSSpatialTick()
    else
        call RSMoverTick()
    endif
    call RSRecord("sample")
    if RS_SCENARIO >= 6 then
        call RSCrowdRecord()
    endif
    if (udg_RSTick == 160 and RS_SCENARIO != 9) or udg_RSTick == 300 then
        call RSRecord("complete")
        call PreloadGenEnd("rs-" + RS_NAME + ".txt")
        call PauseTimer(udg_RSTimer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    call PreloadGenClear()
    call PreloadGenStart()
    call Preload("RSPATIAL tick=0 label=init scenario=" + I2S(RS_SCENARIO))
    set udg_RSTimer = CreateTimer()
    call TimerStart(udg_RSTimer, 0.10, true, function RSTick)
endfunction
