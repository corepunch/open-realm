// Injected into a COPY of Human02Interlude. Tokens are filled by the map builder.
globals
    unit udg_PathProbeUnit = null
    unit udg_PathProbeGate = null
    unit array udg_PathProbeCrowd
    unit udg_PathProbeTarget = null
    unit udg_PathProbeBuilding = null
    unit udg_PathProbeBuilder = null
    fogmodifier udg_PathProbeFog = null
    timer udg_PathProbeTimer = null
    integer udg_PathProbeTick = 0
    constant integer PATH_PROBE_SCENARIO = @SCENARIO@
endglobals

function PathProbeRecord takes string label returns nothing
    local string line = "PATHTRACE tick=" + I2S(udg_PathProbeTick) + " label=" + label
    set line = line + " x=" + R2S(GetUnitX(udg_PathProbeUnit)) + " y=" + R2S(GetUnitY(udg_PathProbeUnit))
    set line = line + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeUnit))
    call Preload(line)
endfunction

function PathProbeWall takes boolean passable returns nothing
    local integer i = 0
    loop
        exitwhen i == 4
        call SetTerrainPathable(-2000.0 + I2R(i) * 32.0, -560.0, PATHING_TYPE_WALKABILITY, passable)
        set i = i + 1
    endloop
    if IsTerrainPathable(-1936.0, -560.0, PATHING_TYPE_WALKABILITY) then
        call PathProbeRecord("wall_query_true")
    else
        call PathProbeRecord("wall_query_false")
    endif
endfunction

// Public admission witness; instantaneous snapshots and intervening samples
// distinguish persistent commands from metadata and automatic sub-behaviors.
function PathProbeOrderLifecycle takes nothing returns nothing
    local boolean accepted = false
    local string label = ""
    if udg_PathProbeTick == 10 then
        set label = "point_move"
        set accepted = IssuePointOrder(udg_PathProbeUnit, "move", -1936.0, -144.0)
    elseif udg_PathProbeTick == 30 then
        set label = "hold"
        set accepted = IssueImmediateOrder(udg_PathProbeUnit, "holdposition")
    elseif udg_PathProbeTick == 60 then
        set label = "defend"
        set accepted = IssueImmediateOrder(udg_PathProbeUnit, "defend")
    elseif udg_PathProbeTick == 65 then
        set label = "undefend"
        set accepted = IssueImmediateOrder(udg_PathProbeUnit, "undefend")
    elseif udg_PathProbeTick == 80 then
        set label = "target_smart"
        set accepted = IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget)
    elseif udg_PathProbeTick == 110 then
        call SetUnitX(udg_PathProbeTarget, GetUnitX(udg_PathProbeUnit) + 32.0)
        call SetUnitY(udg_PathProbeTarget, GetUnitY(udg_PathProbeUnit))
        call PathProbeRecord("target_near")
    elseif udg_PathProbeTick == 120 then
        set label = "target_move"
        set accepted = IssueTargetOrder(udg_PathProbeUnit, "move", udg_PathProbeTarget)
    elseif udg_PathProbeTick == 140 then
        set label = "stop"
        set accepted = IssueImmediateOrder(udg_PathProbeUnit, "stop")
    elseif udg_PathProbeTick == 130 then
        call RemoveUnit(udg_PathProbeTarget)
        call PathProbeRecord("target_removed")
    elseif udg_PathProbeTick == 150 then
        set label = "patrol"
        set accepted = IssuePointOrder(udg_PathProbeUnit, "patrol", -1936.0, -144.0)
    elseif udg_PathProbeTick == 170 then
        set label = "invalid"
        set accepted = IssuePointOrder(udg_PathProbeUnit, "missingorder", -1936.0, -144.0)
    elseif udg_PathProbeTick == 180 then
        set label = "hold_again"
        set accepted = IssueImmediateOrder(udg_PathProbeUnit, "holdposition")
    elseif udg_PathProbeTick == 200 then
        set udg_PathProbeBuilding = CreateUnit(Player(1), 'hfoo', GetUnitX(udg_PathProbeUnit) + 32.0, GetUnitY(udg_PathProbeUnit), 0.0)
        call PauseUnit(udg_PathProbeBuilding, true)
        call Preload("PATHHOLD tick=200 life=" + R2S(GetUnitState(udg_PathProbeBuilding, UNIT_STATE_LIFE)))
        call PathProbeRecord("enemy_near")
    elseif udg_PathProbeTick == 220 then
        call Preload("PATHHOLD tick=220 life=" + R2S(GetUnitState(udg_PathProbeBuilding, UNIT_STATE_LIFE)))
        set udg_PathProbeTarget = CreateUnit(Player(0), 'hfoo', -1936.0, -144.0, 0.0)
    elseif udg_PathProbeTick == 230 then
        set label = "follow_before_death"
        set accepted = IssueTargetOrder(udg_PathProbeUnit, "move", udg_PathProbeTarget)
    elseif udg_PathProbeTick == 240 then
        call KillUnit(udg_PathProbeUnit)
        call PathProbeRecord("killed")
    elseif udg_PathProbeTick == 250 then
        set label = "dead_move"
        set accepted = IssueTargetOrder(udg_PathProbeUnit, "move", udg_PathProbeTarget)
    endif
    if label != "" then
        if accepted then
            call PathProbeRecord(label + "_accepted")
        else
            call PathProbeRecord(label + "_rejected")
        endif
    endif
endfunction

function PathProbeNumericInputs takes nothing returns nothing
    local real realResult = 0.0
    local integer integerResult = 0
@NUMERIC_CASES@
endfunction

function PathProbeAngleInputs takes nothing returns nothing
    local real realResult = 0.0
    local integer integerResult = 0
@ANGLE_CASES@
endfunction

function PathProbePowerInputs takes nothing returns nothing
    local real realResult = 0.0
@POWER_CASES@
endfunction

function PathProbeLiteralInputs takes nothing returns nothing
    local real realResult = 0.0
    local integer integerResult = 0
@LITERAL_CASES@
endfunction

function PathProbeIntegerInputs takes nothing returns nothing
    local real realResult = 0.0
@INTEGER_CASES@
endfunction

function PathProbeTick takes nothing returns nothing
    local integer targetVisible = 0
    local integer crowdIndex = 0
    local real numericX = 0.0
    local real numericY = 0.0
    set udg_PathProbeTick = udg_PathProbeTick + 1
    if PATH_PROBE_SCENARIO == 23 and udg_PathProbeTick == 1 then
        call PathProbeNumericInputs()
    endif
    if PATH_PROBE_SCENARIO == 24 and udg_PathProbeTick == 1 then
        call PathProbeAngleInputs()
    endif
    if PATH_PROBE_SCENARIO == 27 and udg_PathProbeTick == 1 then
        call PathProbePowerInputs()
    endif
    if PATH_PROBE_SCENARIO == 28 and udg_PathProbeTick == 1 then
        call PathProbeLiteralInputs()
    endif
    if PATH_PROBE_SCENARIO == 29 and udg_PathProbeTick == 1 then
        call PathProbeIntegerInputs()
    endif
    if PATH_PROBE_SCENARIO == 22 then
        call PathProbeOrderLifecycle()
    endif
    if PATH_PROBE_SCENARIO == 26 and udg_PathProbeTick == 5 then
        call PathProbeRecord("before_widget_build_ramp")
        if IssueBuildOrderById(udg_PathProbeBuilder, 'htow', GetUnitX(udg_PathProbeUnit), GetUnitY(udg_PathProbeUnit)) then
            call PathProbeRecord("widget_build_ramp_accepted")
        else
            call PathProbeRecord("widget_build_ramp_rejected")
        endif
    endif
    if PATH_PROBE_SCENARIO == 26 and udg_PathProbeTick == 10 then
        // Original W3E: this5x5 vertex rectangle has one terrain layer,
        // no ramp/water/boundary flags. Keep all original map data intact.
        call SetUnitPosition(udg_PathProbeUnit, -2560.0, -1024.0)
        call SetUnitPosition(udg_PathProbeBuilder, -2864.0, -1024.0)
        call PathProbeRecord("before_widget_build")
        if IssueBuildOrderById(udg_PathProbeBuilder, 'htow', GetUnitX(udg_PathProbeUnit), GetUnitY(udg_PathProbeUnit)) then
            call PathProbeRecord("widget_build_accepted")
        else
            call PathProbeRecord("widget_build_rejected")
        endif
    endif
    if PATH_PROBE_SCENARIO == 25 and udg_PathProbeTick == 10 then
        call PathProbeRecord("before_widget_create")
        set udg_PathProbeBuilding = CreateUnit(Player(0), 'htow', GetUnitX(udg_PathProbeUnit), GetUnitY(udg_PathProbeUnit), 0.0)
        if udg_PathProbeBuilding != null then
            call PathProbeRecord("after_widget_create")
            call Preload("PATHWIDGET tick=10 x=" + R2S(GetUnitX(udg_PathProbeBuilding)) + " y=" + R2S(GetUnitY(udg_PathProbeBuilding)))
        else
            call PathProbeRecord("widget_create_failed")
        endif
    endif
    if PATH_PROBE_SCENARIO == 25 and udg_PathProbeTick == 20 then
        call PathProbeRecord("before_widget_pathing_off")
        call SetUnitPathing(udg_PathProbeBuilding, false)
        call PathProbeRecord("after_widget_pathing_off")
    endif
    if PATH_PROBE_SCENARIO == 25 and udg_PathProbeTick == 30 then
        call PathProbeRecord("before_widget_pathing_on")
        call SetUnitPathing(udg_PathProbeBuilding, true)
        call PathProbeRecord("after_widget_pathing_on")
    endif
    if PATH_PROBE_SCENARIO == 21 and udg_PathProbeTick == 200 then
        call SetUnitTurnSpeed(udg_PathProbeUnit, 0.125)
        call SetUnitPropWindow(udg_PathProbeUnit, 0.5)
        call Preload("PATHSTOCK tick=200 turn=" + R2S(GetUnitTurnSpeed(udg_PathProbeUnit)) + " window=" + R2S(GetUnitPropWindow(udg_PathProbeUnit)) + " defaultTurn=" + R2S(GetUnitDefaultTurnSpeed(udg_PathProbeUnit)) + " defaultWindow=" + R2S(GetUnitDefaultPropWindow(udg_PathProbeUnit)))
    endif
    if (PATH_PROBE_SCENARIO == 19 or PATH_PROBE_SCENARIO == 25) and udg_PathProbeTick == @REMOVE_TICK@ then
        call PathProbeRecord("before_widget_remove")
        call Preload("PATHWIDGET tick=" + I2S(udg_PathProbeTick) + " x=" + R2S(GetUnitX(udg_PathProbeBuilding)) + " y=" + R2S(GetUnitY(udg_PathProbeBuilding)))
        call RemoveUnit(udg_PathProbeBuilding)
        set udg_PathProbeBuilding = null
        call PathProbeRecord("after_widget_remove")
    endif
    if udg_PathProbeTick == 10 and PATH_PROBE_SCENARIO != 22 and PATH_PROBE_SCENARIO != 25 and PATH_PROBE_SCENARIO != 26 then
        call PathProbeRecord("before_order")
        if (PATH_PROBE_SCENARIO >= 10 and PATH_PROBE_SCENARIO <= 15) then
            if IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget) then
                call PathProbeRecord("order_accepted")
            else
                call PathProbeRecord("order_rejected")
            endif
        elseif PATH_PROBE_SCENARIO == 23 then
            call Preload("PATHNUM case=move_x native=S2R")
            set numericX = S2R("-1936.25")
            call Preload("PATHNUM done=move_x value=" + R2S(numericX))
            call Preload("PATHNUM case=move_y native=S2R")
            set numericY = S2R("-144.125")
            call Preload("PATHNUM done=move_y value=" + R2S(numericY))
            if IssuePointOrder(udg_PathProbeUnit, "move", numericX, numericY) then
                call PathProbeRecord("order_accepted")
            else
                call PathProbeRecord("order_rejected")
            endif
        elseif IssuePointOrder(udg_PathProbeUnit, "move", -1936.0, -144.0) then
            call PathProbeRecord("order_accepted")
        else
            call PathProbeRecord("order_rejected")
        endif
    endif
    if PATH_PROBE_SCENARIO == 14 or PATH_PROBE_SCENARIO == 15 then
        if udg_PathProbeTick == 60 then
            call PathProbeRecord("before_fog")
            set udg_PathProbeFog = CreateFogModifierRadius(Player(0), FOG_OF_WAR_FOGGED, -1936.0, -144.0, 600.0, false, true)
            call FogModifierStart(udg_PathProbeFog)
            call PathProbeRecord("fog_started")
        endif
        if (udg_PathProbeTick == 80 and PATH_PROBE_SCENARIO == 14) or (udg_PathProbeTick == 70 and PATH_PROBE_SCENARIO == 15) then
            call PathProbeRecord("before_hidden_shift")
            call SetUnitY(udg_PathProbeTarget, 112.0)
            call PathProbeRecord("after_hidden_shift")
        endif
        if (udg_PathProbeTick == 160 and PATH_PROBE_SCENARIO == 14) or (udg_PathProbeTick == 75 and PATH_PROBE_SCENARIO == 15) then
            call PathProbeRecord("before_visibility_restore")
            call DestroyFogModifier(udg_PathProbeFog)
            call PathProbeRecord("fog_removed")
        endif
        if IsUnitVisible(udg_PathProbeTarget, Player(0)) then
            set targetVisible = 1
        endif
        call Preload("PATHTARGET tick=" + I2S(udg_PathProbeTick) + " x=" + R2S(GetUnitX(udg_PathProbeTarget)) + " y=" + R2S(GetUnitY(udg_PathProbeTarget)) + " visible=" + I2S(targetVisible))
    endif
    if PATH_PROBE_SCENARIO == 13 then
        if udg_PathProbeTick == 60 then
            call PathProbeRecord("before_invisibility")
            if UnitAddAbility(udg_PathProbeTarget, 'Apiv') then
                call PathProbeRecord("invisibility_added")
            else
                call PathProbeRecord("invisibility_add_failed")
            endif
        endif
        if udg_PathProbeTick == 80 then
            call PathProbeRecord("before_hidden_shift")
            call SetUnitY(udg_PathProbeTarget, 112.0)
            call PathProbeRecord("after_hidden_shift")
        endif
        if udg_PathProbeTick == 160 then
            call PathProbeRecord("before_visibility_restore")
            if UnitRemoveAbility(udg_PathProbeTarget, 'Apiv') then
                call PathProbeRecord("invisibility_removed")
            else
                call PathProbeRecord("invisibility_remove_failed")
            endif
        endif
        if IsUnitVisible(udg_PathProbeTarget, Player(0)) then
            set targetVisible = 1
        endif
        call Preload("PATHTARGET tick=" + I2S(udg_PathProbeTick) + " x=" + R2S(GetUnitX(udg_PathProbeTarget)) + " y=" + R2S(GetUnitY(udg_PathProbeTarget)) + " visible=" + I2S(targetVisible))
    endif
    if udg_PathProbeTick == 80 and PATH_PROBE_SCENARIO == 12 then
        call PathProbeRecord("before_target_move")
        if IssuePointOrder(udg_PathProbeTarget, "move", -1936.0, 112.0) then
            call PathProbeRecord("target_move_accepted")
        else
            call PathProbeRecord("target_move_rejected")
        endif
    endif
    if PATH_PROBE_SCENARIO == 12 then
        call Preload("PATHTARGET tick=" + I2S(udg_PathProbeTick) + " x=" + R2S(GetUnitX(udg_PathProbeTarget)) + " y=" + R2S(GetUnitY(udg_PathProbeTarget)) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeTarget)))
    endif
    if udg_PathProbeTick == 80 and PATH_PROBE_SCENARIO == 11 then
        call PathProbeRecord("before_target_shift")
        call SetUnitY(udg_PathProbeTarget, 112.0)
        call Preload("PATHTARGET tick=80 x=" + R2S(GetUnitX(udg_PathProbeTarget)) + " y=" + R2S(GetUnitY(udg_PathProbeTarget)))
        call PathProbeRecord("after_target_shift")
    endif
    if udg_PathProbeTick == 15 and PATH_PROBE_SCENARIO == 9 then
        call PathProbeRecord("before_owner_change")
        call SetUnitOwner(udg_PathProbeUnit, Player(1), false)
        if GetOwningPlayer(udg_PathProbeUnit) == Player(1) then
            call PathProbeRecord("after_owner_change")
        endif
    endif
    if udg_PathProbeTick == 15 and PATH_PROBE_SCENARIO == 7 then
        call PathProbeRecord("before_gate_retarget")
        call WaygateSetDestination(udg_PathProbeGate, -1936.0, -432.0)
        call PathProbeRecord("after_gate_retarget")
    endif
    if udg_PathProbeTick == 15 and PATH_PROBE_SCENARIO == 8 then
        call PathProbeRecord("before_gate_disable")
        call WaygateActivate(udg_PathProbeGate, false)
        if not WaygateIsActive(udg_PathProbeGate) then
            call PathProbeRecord("after_gate_disable")
        endif
    endif
    if udg_PathProbeTick == 20 and PATH_PROBE_SCENARIO == 2 then
        call PathProbeRecord("before_insert")
        call PathProbeWall(false)
        call PathProbeRecord("after_insert")
    endif
    if udg_PathProbeTick == @REMOVE_TICK@ and (PATH_PROBE_SCENARIO == 3 or PATH_PROBE_SCENARIO == 4) then
        call PathProbeRecord("before_remove")
        call PathProbeWall(true)
        call PathProbeRecord("after_remove")
    endif
    if udg_PathProbeTick == @REMOVE_TICK@ + 1 and PATH_PROBE_SCENARIO == 4 then
        call PathProbeRecord("before_reorder")
        if IssueImmediateOrder(udg_PathProbeUnit, "stop") then
            call PathProbeRecord("stop_accepted")
        else
            call PathProbeRecord("stop_rejected")
        endif
        if IssuePointOrder(udg_PathProbeUnit, "move", -1936.0, -144.0) then
            call PathProbeRecord("reorder_accepted")
        else
            call PathProbeRecord("reorder_rejected")
        endif
    endif
    if (PATH_PROBE_SCENARIO == 17 or PATH_PROBE_SCENARIO == 18) then
        loop
            exitwhen crowdIndex == 9
            if udg_PathProbeTick == 10 and crowdIndex != 4 then
                if IssuePointOrder(udg_PathProbeCrowd[crowdIndex], "move", -1936.0, -144.0) then
                    call Preload("PATHCROWD tick=10 id=" + I2S(crowdIndex) + " ordered=1")
                else
                    call Preload("PATHCROWD tick=10 id=" + I2S(crowdIndex) + " ordered=0")
                endif
            endif
            call Preload("PATHCROWD tick=" + I2S(udg_PathProbeTick) + " id=" + I2S(crowdIndex) + " x=" + R2S(GetUnitX(udg_PathProbeCrowd[crowdIndex])) + " y=" + R2S(GetUnitY(udg_PathProbeCrowd[crowdIndex])) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[crowdIndex])))
            set crowdIndex = crowdIndex + 1
        endloop
    endif
    call PathProbeRecord("sample")
    if udg_PathProbeTick == 300 then
        call PathProbeRecord("complete")
        call PreloadGenEnd("pathtrace-@NAME@.txt")
        call PauseTimer(udg_PathProbeTimer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    local integer gx = 0
    local integer gy = 0
    local integer crowdType = 'hfoo'
    call PreloadGenClear()
    call PreloadGenStart()
    if PATH_PROBE_SCENARIO == 26 then
        // The cinematic map configures Player0 as neutral before this hook.
        // Use a human controller before creating the construction worker.
        call SetPlayerController(Player(0), MAP_CONTROL_USER)
    endif
    if PATH_PROBE_SCENARIO == 18 then
        set crowdType = 'hgry'
    endif
    set udg_PathProbeUnit = CreateUnit(Player(0), crowdType, -1936.0, -976.0, 90.0)
    if PATH_PROBE_SCENARIO == 21 then
        call SetUnitFacing(udg_PathProbeUnit, 0.0)
        call Preload("PATHSTOCK tick=0 turn=" + R2S(GetUnitTurnSpeed(udg_PathProbeUnit)) + " window=" + R2S(GetUnitPropWindow(udg_PathProbeUnit)) + " defaultTurn=" + R2S(GetUnitDefaultTurnSpeed(udg_PathProbeUnit)) + " defaultWindow=" + R2S(GetUnitDefaultPropWindow(udg_PathProbeUnit)))
    endif
    if PATH_PROBE_SCENARIO == 22 then
        call SetPlayerTechResearched(Player(0), 'Rhde', 1)
        call SetPlayerController(Player(0), MAP_CONTROL_USER)
        call SetPlayerController(Player(1), MAP_CONTROL_COMPUTER)
        call SetPlayerAlliance(Player(0), Player(1), ALLIANCE_PASSIVE, false)
        call SetPlayerAlliance(Player(1), Player(0), ALLIANCE_PASSIVE, false)
    endif
    if PATH_PROBE_SCENARIO == 20 then
        call SetUnitFacing(udg_PathProbeUnit, 0.0)
        call SetUnitTurnSpeed(udg_PathProbeUnit, 0.125)
        call SetUnitPropWindow(udg_PathProbeUnit, 0.5)
    endif
    if (PATH_PROBE_SCENARIO >= 10 and PATH_PROBE_SCENARIO <= 15) or PATH_PROBE_SCENARIO == 22 then
        if PATH_PROBE_SCENARIO >= 13 and PATH_PROBE_SCENARIO <= 15 then
            call SetPlayerAlliance(Player(0), Player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE, true)
            call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE), Player(0), ALLIANCE_PASSIVE, true)
            set udg_PathProbeTarget = CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE), 'hfoo', -1936.0, -144.0, 90.0)
        else
            set udg_PathProbeTarget = CreateUnit(Player(0), 'hfoo', -1936.0, -144.0, 90.0)
        endif
        if PATH_PROBE_SCENARIO == 12 then
            call SetUnitMoveSpeed(udg_PathProbeTarget, 100.0)
        endif
    endif
    if (PATH_PROBE_SCENARIO == 17 or PATH_PROBE_SCENARIO == 18) then
        loop
            exitwhen gx == 9
            if gx == 4 then
                set udg_PathProbeCrowd[gx] = udg_PathProbeUnit
            else
                set udg_PathProbeCrowd[gx] = CreateUnit(Player(0), crowdType, -2016.0 + I2R(ModuloInteger(gx, 3)) * 80.0, -1056.0 + I2R(gx / 3) * 80.0, 90.0)
            endif
            call SetUnitMoveSpeed(udg_PathProbeCrowd[gx], 100.0)
            set gx = gx + 1
        endloop
    endif
    call SetUnitMoveSpeed(udg_PathProbeUnit, 100.0)
    call FogEnable(PATH_PROBE_SCENARIO == 14 or PATH_PROBE_SCENARIO == 15)
    call FogMaskEnable(false)
    call SetCameraPosition(-1936.0, -560.0)
    call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE, 1800.0, 0.0)
    call SelectUnit(udg_PathProbeUnit, true)
    call PathProbeRecord("start_@NAME@")
    if PATH_PROBE_SCENARIO == 26 then
        call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 100000)
        call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER, 100000)
        set udg_PathProbeBuilder = CreateUnit(Player(0), 'hpea', -2240.0, -976.0, 0.0)
    endif
    if PATH_PROBE_SCENARIO == 19 then
        call PathProbeRecord("before_widget_create")
        set udg_PathProbeBuilding = CreateUnit(Player(0), 'hhou', -1936.0, -560.0, 0.0)
        if udg_PathProbeBuilding != null then
            call PathProbeRecord("after_widget_create")
            call Preload("PATHWIDGET tick=0 x=" + R2S(GetUnitX(udg_PathProbeBuilding)) + " y=" + R2S(GetUnitY(udg_PathProbeBuilding)))
        else
            call PathProbeRecord("widget_create_failed")
        endif
    endif
    if PATH_PROBE_SCENARIO >= 5 and PATH_PROBE_SCENARIO <= 8 then
        set udg_PathProbeGate = CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE), 'nwgt', -1936.0, @GATE_Y@, 0.0)
        call WaygateSetDestination(udg_PathProbeGate, -1936.0, @GATE_EXIT_Y@)
        call WaygateActivate(udg_PathProbeGate, PATH_PROBE_SCENARIO != 6)
        if WaygateIsActive(udg_PathProbeGate) then
            call PathProbeRecord("gate_active")
        else
            call PathProbeRecord("gate_inactive")
        endif
    endif
    if PATH_PROBE_SCENARIO == 1 or PATH_PROBE_SCENARIO == 3 or PATH_PROBE_SCENARIO == 4 then
        call PathProbeWall(false)
    endif
    if PATH_PROBE_SCENARIO == 16 then
        loop
            exitwhen gx == 5
            set gy = 0
            loop
                exitwhen gy == 5
                call SetTerrainPathable(-2000.0 + I2R(gx) * 32.0, -208.0 + I2R(gy) * 32.0, PATHING_TYPE_WALKABILITY, false)
                set gy = gy + 1
            endloop
            set gx = gx + 1
        endloop
        call PathProbeRecord("goal_blocked")
    endif
    set udg_PathProbeTimer = CreateTimer()
    call TimerStart(udg_PathProbeTimer, 0.10, true, function PathProbeTick)
endfunction
