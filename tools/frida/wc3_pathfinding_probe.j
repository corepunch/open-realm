// Injected into a COPY of Human02Interlude. Tokens are filled by the map builder.
globals
    integer udg_MorphCase=0
    integer udg_MorphAbility='Ag01'
    unit udg_PathProbeUnit = null
    unit udg_PathProbeGate = null
    group udg_PathProbeGroup = null
    group udg_PathProbePeerGroup = null
    unit array udg_PathProbeCrowd
    unit udg_PathProbeTarget = null
    unit udg_PathProbeBuilding = null
    unit udg_PathProbeBuilder = null
    item udg_PathProbeBootsOne = null
    item udg_PathProbeBootsTwo = null
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

function PathProbeByteInputs takes nothing returns nothing
    local real realResult = 0.0
    local string byteSource = @BYTE_SOURCE@
@BYTE_CASES@
endfunction

function PathProbeSpeedInputs takes unit u, string label returns nothing
    local real observed = 0.0
    call Preload("PATHSPEED case=" + label + "_default")
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_n10")
    call SetUnitMoveSpeed(u, -10.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_0")
    call SetUnitMoveSpeed(u, 0.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_1")
    call SetUnitMoveSpeed(u, 1.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_100")
    call SetUnitMoveSpeed(u, 100.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_149")
    call SetUnitMoveSpeed(u, 149.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_150")
    call SetUnitMoveSpeed(u, 150.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_151")
    call SetUnitMoveSpeed(u, 151.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_399")
    call SetUnitMoveSpeed(u, 399.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_400")
    call SetUnitMoveSpeed(u, 400.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_401")
    call SetUnitMoveSpeed(u, 401.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_522")
    call SetUnitMoveSpeed(u, 522.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED case=" + label + "_1000")
    call SetUnitMoveSpeed(u, 1000.0)
    set observed = GetUnitMoveSpeed(u)
    set observed = GetUnitDefaultMoveSpeed(u)
    call Preload("PATHSPEED done=" + label)
endfunction

// Same-word and fractional axis setters during travel and after Stop.
function PathProbeAxisPosition takes nothing returns nothing
    local string label = ""
    local real x = 0.0
    local real y = 0.0
    if udg_PathProbeTick == 20 then
        set label = "moving_x_same"
    elseif udg_PathProbeTick == 25 then
        set label = "moving_y_same"
    elseif udg_PathProbeTick == 30 then
        set label = "moving_x_shift"
    elseif udg_PathProbeTick == 35 then
        set label = "moving_y_shift"
    elseif udg_PathProbeTick == 70 then
        set label = "idle_x_same"
    elseif udg_PathProbeTick == 75 then
        set label = "idle_y_same"
    elseif udg_PathProbeTick == 80 then
        set label = "idle_x_shift"
    elseif udg_PathProbeTick == 85 then
        set label = "idle_y_shift"
    endif
    if label != "" then
        call Preload("PATHPOSE case=" + label)
        set x = GetUnitX(udg_PathProbeUnit)
        set y = GetUnitY(udg_PathProbeUnit)
        if udg_PathProbeTick == 20 or udg_PathProbeTick == 70 then
            call SetUnitX(udg_PathProbeUnit, x)
        elseif udg_PathProbeTick == 25 or udg_PathProbeTick == 75 then
            call SetUnitY(udg_PathProbeUnit, y)
        elseif udg_PathProbeTick == 30 or udg_PathProbeTick == 80 then
            call SetUnitX(udg_PathProbeUnit, x + 0.125)
        else
            call SetUnitY(udg_PathProbeUnit, y - 0.125)
        endif
        set x = GetUnitX(udg_PathProbeUnit)
        set y = GetUnitY(udg_PathProbeUnit)
        call Preload("PATHPOSE done=" + label)
    endif
    if udg_PathProbeTick == 60 then
        if IssueImmediateOrder(udg_PathProbeUnit, "stop") then
            call PathProbeRecord("axis_stop_accepted")
        else
            call PathProbeRecord("axis_stop_rejected")
        endif
    endif
endfunction

function PathProbeForcedPosition takes nothing returns nothing
    local string label = ""
    local real x = 0.0
    local real y = 0.0
    if udg_PathProbeTick == 20 then
        set label = "moving_same"
    elseif udg_PathProbeTick == 40 then
        set label = "moving_fractional"
    elseif udg_PathProbeTick == 50 then
        set label = "idle_same"
    elseif udg_PathProbeTick == 70 then
        set label = "patrol_fractional"
    endif
    if label != "" then
        call Preload("PATHPOSE case=" + label)
        set x = GetUnitX(udg_PathProbeUnit)
        set y = GetUnitY(udg_PathProbeUnit)
        call PathProbeRecord("forced_before_" + label)
        if udg_PathProbeTick == 40 or udg_PathProbeTick == 70 then
            call SetUnitPosition(udg_PathProbeUnit, x + 0.125, y - 0.125)
        else
            call SetUnitPosition(udg_PathProbeUnit, x, y)
        endif
        call PathProbeRecord("forced_after_" + label)
        call Preload("PATHPOSE done=" + label)
    endif
    if udg_PathProbeTick == 30 then
        if IssuePointOrder(udg_PathProbeUnit, "move", -1600.0, -144.0) then
            call PathProbeRecord("forced_move_reissued")
        endif
    elseif udg_PathProbeTick == 60 then
        if IssuePointOrder(udg_PathProbeUnit, "patrol", -1600.0, -144.0) then
            call PathProbeRecord("forced_patrol_reissued")
        endif
    endif
endfunction

function PathProbeBlockedPosition takes nothing returns nothing
    local integer x = 0
    local integer y = 0
    local string label = ""
    local real targetX = -1936.0
    local real targetY = -512.0
    if udg_PathProbeTick == 20 or udg_PathProbeTick == 45 then
        loop
            exitwhen x == 5
            set y = 0
            loop
                exitwhen y == 5
                if udg_PathProbeTick == 45 or (x > 0 and x < 4 and y > 0 and y < 4) then
                    call SetTerrainPathable(-2000.0 + I2R(x) * 32.0, -576.0 + I2R(y) * 32.0, PATHING_TYPE_WALKABILITY, false)
                endif
                set y = y + 1
            endloop
            set x = x + 1
        endloop
    endif
    if udg_PathProbeTick == 20 then
        set label = "blocked_centre"
    elseif udg_PathProbeTick == 25 then
        set label = "blocked_fractional"
        set targetX = targetX + 0.125
        set targetY = targetY - 0.125
    elseif udg_PathProbeTick == 30 then
        set label = "blocked_west"
        set targetX = targetX - 32.0
    elseif udg_PathProbeTick == 35 then
        set label = "blocked_east"
        set targetX = targetX + 32.0
    elseif udg_PathProbeTick == 40 then
        set label = "blocked_north"
        set targetY = targetY + 32.0
    elseif udg_PathProbeTick == 50 then
        set label = "blocked_large"
    endif
    if label != "" then
        call Preload("PATHPOSE case=" + label)
        call PathProbeRecord("placement_before_" + label)
        call SetUnitPosition(udg_PathProbeUnit, targetX, targetY)
        call PathProbeRecord("placement_after_" + label)
        call Preload("PATHPOSE done=" + label)
    endif
endfunction

function PathProbeRandom takes nothing returns nothing
    local integer integerResult
    local real realResult
@RANDOM_CASES@
endfunction

// CreateUnit must supply a legal first fine source around the retained idle
// Footman. Separate cases by simulation ticks so previous removal is visible.
function PathProbeSpawnAdmission takes nothing returns nothing
    local integer i = (udg_PathProbeTick - 1) / 20
    local real offset = 0.0
    local real value = 0.0
    if ModuloInteger(udg_PathProbeTick - 1, 20) != 0 or i > 7 then
        return
    endif
    if udg_PathProbeTarget != null then
        call RemoveUnit(udg_PathProbeTarget)
    endif
    if i == 1 then
        set offset = 16.0
    elseif i == 2 then
        set offset = 31.0
    elseif i == 3 then
        set offset = 32.0
    elseif i == 4 then
        set offset = 60.0
    elseif i == 5 then
        set offset = 63.9999
    elseif i == 6 then
        set offset = 64.0
    elseif i == 7 then
        set offset = 96.0
    endif
    call Preload("PATHPOSE case=spawn_" + I2S(i))
    set udg_PathProbeTarget = CreateUnit(Player(0), 'hfoo', -1936.0 + offset, -976.0, 0.0)
    set value = GetUnitX(udg_PathProbeTarget)
    set value = GetUnitY(udg_PathProbeTarget)
    set value = GetUnitX(udg_PathProbeUnit)
    set value = GetUnitY(udg_PathProbeUnit)
    if IssuePointOrder(udg_PathProbeTarget, "move", -1632.0, -976.0) then
        call PathProbeRecord("spawn_order_accepted")
    else
        call PathProbeRecord("spawn_order_rejected")
    endif
    call Preload("PATHPOSE done=spawn_" + I2S(i))
endfunction

function PathProbePublicOblique takes nothing returns nothing
    local string label = ""
    local real sourceX = -1936.0
    local real sourceY = -976.0
    local real goalX = -1600.0
    local real goalY = -144.0
    local real value = 0.0
    if udg_PathProbeTick == 1 then
        set label = "oblique_base"
    elseif udg_PathProbeTick == 101 then
        set label = "oblique_small"
        set sourceX = -0.125
        set sourceY = -0.125
        set goalX = 479.875
        set goalY = 832.25
    elseif udg_PathProbeTick == 201 then
        set label = "oblique_large"
        set sourceX = -6000.125
        set sourceY = -1500.25
        set goalX = -5200.375
        set goalY = -832.5
    else
        return
    endif
    call RemoveUnit(udg_PathProbeUnit)
    call Preload("PATHPOSE case=" + label)
    set udg_PathProbeUnit = CreateUnit(Player(0), 'hfoo', sourceX, sourceY, 90.0)
    call SetUnitMoveSpeed(udg_PathProbeUnit, 100.0)
    set value = GetUnitX(udg_PathProbeUnit)
    set value = GetUnitY(udg_PathProbeUnit)
    if IssuePointOrder(udg_PathProbeUnit, "move", goalX, goalY) then
        call PathProbeRecord("oblique_order_accepted")
    else
        call PathProbeRecord("oblique_order_rejected")
    endif
    call Preload("PATHPOSE done=" + label)
endfunction

function PathProbeGroupOrders takes nothing returns nothing
    local integer i = 0
    local string form = ""
    local boolean accepted = false
    local location point = null
    if udg_PathProbeTick == 10 then
        set form = "name"
        set accepted = GroupPointOrder(udg_PathProbeGroup, "move", -1600.0, -144.0)
    elseif udg_PathProbeTick == 80 then
        set form = "id"
        set accepted = GroupPointOrderById(udg_PathProbeGroup, 851986, -1936.0, -976.0)
    elseif udg_PathProbeTick == 150 then
        set form = "loc"
        set point = Location(-1600.0, -144.0)
        set accepted = GroupPointOrderLoc(udg_PathProbeGroup, "move", point)
        call RemoveLocation(point)
    elseif udg_PathProbeTick == 220 then
        set form = "idloc"
        set point = Location(-1936.0, -976.0)
        set accepted = GroupPointOrderByIdLoc(udg_PathProbeGroup, 851986, point)
        call RemoveLocation(point)
    else
        return
    endif
    if accepted then
        call Preload("PATHGROUP tick=" + I2S(udg_PathProbeTick) + " form=" + form + " accepted=1")
    else
        call Preload("PATHGROUP tick=" + I2S(udg_PathProbeTick) + " form=" + form + " accepted=0")
    endif
    loop
        exitwhen i == 14
        call Preload("PATHGROUP tick=" + I2S(udg_PathProbeTick) + " member=" + I2S(i) + " handle=" + I2S(GetHandleId(udg_PathProbeCrowd[i])) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[i])) + " x=" + R2S(GetUnitX(udg_PathProbeCrowd[i])) + " y=" + R2S(GetUnitY(udg_PathProbeCrowd[i])))
        set i = i + 1
    endloop
endfunction

function PathProbeTick takes nothing returns nothing
    local integer targetVisible = 0
    local integer crowdIndex = 0
    local real numericX = 0.0
    local real numericY = 0.0
    set udg_PathProbeTick = udg_PathProbeTick + 1
    if PATH_PROBE_SCENARIO == 71 and udg_PathProbeTick == 10 then
        call PathProbeRecord("before_captain_ai")
        call StartCampaignAI(Player(0),"Scripts\\wc3_captain_probe.ai")
        call PathProbeRecord("after_captain_ai")
    endif
    if PATH_PROBE_SCENARIO == 44 then
        call PathProbeSpawnAdmission()
    elseif PATH_PROBE_SCENARIO == 45 then
        call PathProbePublicOblique()
    elseif PATH_PROBE_SCENARIO == 46 then
        call PathProbeGroupOrders()
    elseif PATH_PROBE_SCENARIO == 48 then
        if udg_PathProbeTick == 10 then
            if GroupPointOrder(udg_PathProbeGroup, "move", -1872.0, -528.0) then
                call Preload("PATHDOZEN tick=10 accepted=1")
            else
                call Preload("PATHDOZEN tick=10 accepted=0")
            endif
        endif
        set crowdIndex = 0
        loop
            exitwhen crowdIndex == 12
            call Preload("PATHDOZEN tick=" + I2S(udg_PathProbeTick) + " member=" + I2S(crowdIndex) + " x=" + R2S(GetUnitX(udg_PathProbeCrowd[crowdIndex])) + " y=" + R2S(GetUnitY(udg_PathProbeCrowd[crowdIndex])) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[crowdIndex])))
            set crowdIndex = crowdIndex + 1
        endloop
    elseif PATH_PROBE_SCENARIO>=65 and PATH_PROBE_SCENARIO<=67 then
        if udg_PathProbeTick==10 then
            if GroupPointOrder(udg_PathProbeGroup,"move",-1936.0,-144.0) then
                call PathProbeRecord("group_radius_order_accepted")
            else
                call PathProbeRecord("group_radius_order_rejected")
            endif
        endif
    elseif PATH_PROBE_SCENARIO == 47 then
        if udg_PathProbeTick == 10 then
            if GroupPointOrder(udg_PathProbeGroup, "move", -1936.0, -720.0) then
                call Preload("PATHPAIR tick=10 accepted=1")
            else
                call Preload("PATHPAIR tick=10 accepted=0")
            endif
        endif
        set crowdIndex = 0
        loop
            exitwhen crowdIndex == 2
            call Preload("PATHPAIR tick=" + I2S(udg_PathProbeTick) + " member=" + I2S(crowdIndex) + " x=" + R2S(GetUnitX(udg_PathProbeCrowd[crowdIndex])) + " y=" + R2S(GetUnitY(udg_PathProbeCrowd[crowdIndex])) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[crowdIndex])))
            set crowdIndex = crowdIndex + 1
        endloop
    endif
    if PATH_PROBE_SCENARIO == 32 and udg_PathProbeTick == 1 then
        call PathProbeSpeedInputs(udg_PathProbeUnit, "foot")
        call PathProbeSpeedInputs(udg_PathProbeCrowd[0], "custom")
        call PathProbeSpeedInputs(udg_PathProbeCrowd[1], "disabled")
        call SetUnitMoveSpeed(udg_PathProbeUnit, 100.0)
    endif
    if (PATH_PROBE_SCENARIO == 32 and udg_PathProbeTick == 50) or (PATH_PROBE_SCENARIO == 33 and udg_PathProbeTick == 20) then
        call Preload("PATHSPEED case=foot_travel_high")
        call SetUnitMoveSpeed(udg_PathProbeUnit, 401.0)
        set numericX = GetUnitMoveSpeed(udg_PathProbeUnit)
        set numericY = GetUnitDefaultMoveSpeed(udg_PathProbeUnit)
        call Preload("PATHSPEED done=travel")
    endif
    if (PATH_PROBE_SCENARIO == 32 and udg_PathProbeTick == 70) or (PATH_PROBE_SCENARIO == 33 and udg_PathProbeTick == 30) then
        call Preload("PATHSPEED case=foot_travel_low")
        call SetUnitMoveSpeed(udg_PathProbeUnit, 100.0)
        set numericX = GetUnitMoveSpeed(udg_PathProbeUnit)
        set numericY = GetUnitDefaultMoveSpeed(udg_PathProbeUnit)
        call Preload("PATHSPEED done=travel")
    endif
    if PATH_PROBE_SCENARIO == 43 and (udg_PathProbeTick == 20 or udg_PathProbeTick == 30 or udg_PathProbeTick == 40 or udg_PathProbeTick == 50) then
        if udg_PathProbeTick >= 30 then
            if udg_PathProbeTick == 50 then
                set crowdIndex = -5
                loop
                    exitwhen crowdIndex > 5
                    set targetVisible = -5
                    loop
                        exitwhen targetVisible > 5
                        call SetTerrainPathable(-1936.0 + I2R(crowdIndex)*32.0, -560.0 + I2R(targetVisible)*32.0, PATHING_TYPE_WALKABILITY, false)
                        set targetVisible = targetVisible + 1
                    endloop
                    set crowdIndex = crowdIndex + 1
                endloop
            endif
            call SetUnitPathing(udg_PathProbeUnit, false)
            call SetUnitPosition(udg_PathProbeUnit, -1936.0, -560.0)
            if udg_PathProbeTick != 40 then
                call SetUnitPathing(udg_PathProbeUnit, true)
            endif
        endif
        call Preload("PATHPOSE case=stop_recovery_" + I2S(udg_PathProbeTick))
        call PathProbeRecord("stop_recovery_before")
        if IssueImmediateOrder(udg_PathProbeUnit, "stop") then
            call PathProbeRecord("stop_recovery_accepted")
        else
            call PathProbeRecord("stop_recovery_rejected")
        endif
        call PathProbeRecord("stop_recovery_after")
        call Preload("PATHPOSE done=stop_recovery_" + I2S(udg_PathProbeTick))
    endif
    if PATH_PROBE_SCENARIO == 42 and (udg_PathProbeTick == 20 or udg_PathProbeTick == 30 or udg_PathProbeTick == 40 or udg_PathProbeTick == 50 or udg_PathProbeTick == 60) then
        if udg_PathProbeTick == 30 or udg_PathProbeTick == 50 then
            call SetUnitPathing(udg_PathProbeUnit, false)
        else
            call SetUnitPathing(udg_PathProbeUnit, true)
        endif
        call Preload("PATHPOSE case=pathing_position_" + I2S(udg_PathProbeTick))
        call PathProbeRecord("pathing_position_before")
        if udg_PathProbeTick >= 50 then
            call SetUnitPosition(udg_PathProbeUnit, -1935.875, -560.125)
        else
            call SetUnitPosition(udg_PathProbeUnit, -1936.0, -560.0)
        endif
        call PathProbeRecord("pathing_position_after")
        call Preload("PATHPOSE done=pathing_position_" + I2S(udg_PathProbeTick))
    endif
    if PATH_PROBE_SCENARIO == 41 then
        if udg_PathProbeTick == 10 or udg_PathProbeTick == 30 then
            call PathProbeRecord("pathing_disable_before")
            call SetUnitPathing(udg_PathProbeUnit, false)
            call PathProbeRecord("pathing_disable_after")
        elseif udg_PathProbeTick == 20 or udg_PathProbeTick == 40 then
            call PathProbeRecord("pathing_enable_before")
            call SetUnitPathing(udg_PathProbeUnit, true)
            call PathProbeRecord("pathing_enable_after")
        endif
    endif
    if PATH_PROBE_SCENARIO == 40 and udg_PathProbeTick == 1 then
        call PathProbeRandom()
    endif
    if PATH_PROBE_SCENARIO == 36 then
        call PathProbeAxisPosition()
    elseif PATH_PROBE_SCENARIO == 38 then
        call PathProbeForcedPosition()
    elseif PATH_PROBE_SCENARIO == 39 then
        call PathProbeBlockedPosition()
    endif
    if (PATH_PROBE_SCENARIO == 34 or PATH_PROBE_SCENARIO == 35) then
        if udg_PathProbeTick == 1 then
            call Preload("PATHSPEED case=item_baseline")
        elseif udg_PathProbeTick == 20 then
            call Preload("PATHSPEED case=item_one")
            if not UnitAddItem(udg_PathProbeUnit, udg_PathProbeBootsOne) then
                call PathProbeRecord("item_one_failed")
            endif
        elseif PATH_PROBE_SCENARIO == 35 and udg_PathProbeTick == 22 then
            call Preload("PATHSPEED case=item_publish_set")
            call SetUnitMoveSpeed(udg_PathProbeUnit, 270.0)
        elseif PATH_PROBE_SCENARIO == 35 and udg_PathProbeTick == 23 then
            call Preload("PATHSPEED case=item_reissue")
            if not IssuePointOrder(udg_PathProbeUnit, "move", -1936.0, -144.0) then
                call PathProbeRecord("item_reissue_failed")
            endif
        elseif udg_PathProbeTick == 25 then
            call Preload("PATHSPEED case=item_two")
            if not UnitAddItem(udg_PathProbeUnit, udg_PathProbeBootsTwo) then
                call PathProbeRecord("item_two_failed")
            endif
        elseif udg_PathProbeTick == 30 then
            call Preload("PATHSPEED case=item_remove_one")
            call UnitRemoveItem(udg_PathProbeUnit, udg_PathProbeBootsOne)
        elseif udg_PathProbeTick == 35 then
            call Preload("PATHSPEED case=item_remove_two")
            call UnitRemoveItem(udg_PathProbeUnit, udg_PathProbeBootsTwo)
        elseif PATH_PROBE_SCENARIO == 35 and udg_PathProbeTick == 36 then
            call Preload("PATHSPEED case=item_publish_restore")
            call SetUnitMoveSpeed(udg_PathProbeUnit, 270.0)
        endif
        if udg_PathProbeTick == 1 or udg_PathProbeTick == 20 or udg_PathProbeTick == 25 or udg_PathProbeTick == 30 or udg_PathProbeTick == 35 or (PATH_PROBE_SCENARIO == 35 and (udg_PathProbeTick == 22 or udg_PathProbeTick == 23 or udg_PathProbeTick == 36)) then
            set numericX = GetUnitMoveSpeed(udg_PathProbeUnit)
            set numericY = GetUnitDefaultMoveSpeed(udg_PathProbeUnit)
            call Preload("PATHSPEED done=item")
        endif
    endif
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
    if PATH_PROBE_SCENARIO == 30 and udg_PathProbeTick == 1 then
        call PathProbeByteInputs()
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
    if PATH_PROBE_SCENARIO == 69 and udg_PathProbeTick == 5 then
        call Preload("PATHBOUND case=0 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-7040.00048828,-976.00000000) then
            call Preload("PATHBOUND case=0 accepted")
        else
            call Preload("PATHBOUND case=0 rejected")
        endif
        call Preload("PATHBOUND case=1 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-7040.00000000,-976.00000000) then
            call Preload("PATHBOUND case=1 accepted")
        else
            call Preload("PATHBOUND case=1 rejected")
        endif
        call Preload("PATHBOUND case=2 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-7039.99951172,-976.00000000) then
            call Preload("PATHBOUND case=2 accepted")
        else
            call Preload("PATHBOUND case=2 rejected")
        endif
        call Preload("PATHBOUND case=3 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",4991.99951172,-976.00000000) then
            call Preload("PATHBOUND case=3 accepted")
        else
            call Preload("PATHBOUND case=3 rejected")
        endif
        call Preload("PATHBOUND case=4 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",4992.00000000,-976.00000000) then
            call Preload("PATHBOUND case=4 accepted")
        else
            call Preload("PATHBOUND case=4 rejected")
        endif
        call Preload("PATHBOUND case=5 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",4992.00048828,-976.00000000) then
            call Preload("PATHBOUND case=5 accepted")
        else
            call Preload("PATHBOUND case=5 rejected")
        endif
        call Preload("PATHBOUND case=6 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,-2944.00048828) then
            call Preload("PATHBOUND case=6 accepted")
        else
            call Preload("PATHBOUND case=6 rejected")
        endif
        call Preload("PATHBOUND case=7 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,-2944.00000000) then
            call Preload("PATHBOUND case=7 accepted")
        else
            call Preload("PATHBOUND case=7 rejected")
        endif
        call Preload("PATHBOUND case=8 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,-2943.99951172) then
            call Preload("PATHBOUND case=8 accepted")
        else
            call Preload("PATHBOUND case=8 rejected")
        endif
        call Preload("PATHBOUND case=9 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,4991.99951172) then
            call Preload("PATHBOUND case=9 accepted")
        else
            call Preload("PATHBOUND case=9 rejected")
        endif
        call Preload("PATHBOUND case=10 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,4992.00000000) then
            call Preload("PATHBOUND case=10 accepted")
        else
            call Preload("PATHBOUND case=10 rejected")
        endif
        call Preload("PATHBOUND case=11 before")
        if IssuePointOrder(udg_PathProbeUnit,"move",-1936.00000000,4992.00048828) then
            call Preload("PATHBOUND case=11 accepted")
        else
            call Preload("PATHBOUND case=11 rejected")
        endif
        call IssueImmediateOrder(udg_PathProbeUnit,"stop")
        call PathProbeRecord("bounds_complete")
    endif
    if PATH_PROBE_SCENARIO == 68 and udg_PathProbeTick == 10 then
        call PathProbeRecord("before_outside_order")
        if IssuePointOrder(udg_PathProbeUnit,"move",-7400.0,-976.0) then
            call PathProbeRecord("outside_order_accepted")
        else
            call PathProbeRecord("outside_order_rejected")
        endif
    endif
    if udg_PathProbeTick == 10 and PATH_PROBE_SCENARIO != 22 and PATH_PROBE_SCENARIO != 25 and PATH_PROBE_SCENARIO != 26 and PATH_PROBE_SCENARIO != 44 and PATH_PROBE_SCENARIO != 45 and PATH_PROBE_SCENARIO != 46 and PATH_PROBE_SCENARIO != 47 and PATH_PROBE_SCENARIO != 48 and PATH_PROBE_SCENARIO < 65 then
        call PathProbeRecord("before_order")
        if ((PATH_PROBE_SCENARIO >= 10 and PATH_PROBE_SCENARIO <= 15) or (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62)) then
            if IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget) then
                call PathProbeRecord("order_accepted")
            else
                call PathProbeRecord("order_rejected")
            endif
        elseif PATH_PROBE_SCENARIO == 23 or PATH_PROBE_SCENARIO == 30 then
            call Preload("PATHNUM case=move_x native=S2R")
            if PATH_PROBE_SCENARIO == 30 then
                set numericX = S2R("-1936.25" + SubString(GetUnitName(udg_PathProbeUnit), 0, 1) + "9")
            else
                set numericX = S2R("-1936.25")
            endif
            call Preload("PATHNUM done=move_x value=" + R2S(numericX))
            call Preload("PATHNUM case=move_y native=S2R")
            if PATH_PROBE_SCENARIO == 30 then
                set numericY = S2R("-144.125" + SubString(GetUnitName(udg_PathProbeUnit), 127, 128) + "9")
            else
                set numericY = S2R("-144.125")
            endif
            call Preload("PATHNUM done=move_y value=" + R2S(numericY))
            if IssuePointOrder(udg_PathProbeUnit, "move", numericX, numericY) then
                call PathProbeRecord("order_accepted")
            else
                call PathProbeRecord("order_rejected")
            endif
        elseif PATH_PROBE_SCENARIO == 36 or PATH_PROBE_SCENARIO == 37 or PATH_PROBE_SCENARIO == 38 then
            if IssuePointOrder(udg_PathProbeUnit, "move", -1600.0, -144.0) then
                call PathProbeRecord("order_accepted")
            else
                call PathProbeRecord("order_rejected")
            endif
        elseif PATH_PROBE_SCENARIO == 49 or PATH_PROBE_SCENARIO == 50 or (PATH_PROBE_SCENARIO == 51 or PATH_PROBE_SCENARIO == 52) then
            call Preload("PATHSELECT tick=10 ready local=" + I2S(GetPlayerId(GetLocalPlayer())))
            if PATH_PROBE_SCENARIO == 50 or (PATH_PROBE_SCENARIO == 51 or PATH_PROBE_SCENARIO == 52) then
                call PathProbeRecord("before_queued_first_group")
                if GroupPointOrder(udg_PathProbeGroup, "move", -1936.0, -144.0) then
                    call PathProbeRecord("queued_first_group_accepted")
                else
                    call PathProbeRecord("queued_first_group_rejected")
                endif
            endif
            if PATH_PROBE_SCENARIO == 52 then
                if GroupPointOrder(udg_PathProbePeerGroup, "move", -1856.0, -144.0) then
                    call PathProbeRecord("independent_peer_group_accepted")
                else
                    call PathProbeRecord("independent_peer_group_rejected")
                endif
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
    if (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62) and udg_PathProbeTick == 85 then
        call PathProbeRecord("before_target_speed")
        call SetUnitMoveSpeed(udg_PathProbeTarget, 300.0)
        call PathProbeRecord("after_target_speed")
    endif
    if udg_PathProbeTick == 80 and (PATH_PROBE_SCENARIO == 12 or (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62)) then
        call PathProbeRecord("before_target_move")
        if IssuePointOrder(udg_PathProbeTarget, "move", -1936.0, 112.0) then
            call PathProbeRecord("target_move_accepted")
        else
            call PathProbeRecord("target_move_rejected")
        endif
    endif
    if ((PATH_PROBE_SCENARIO == 56 or PATH_PROBE_SCENARIO == 57) and udg_PathProbeTick == 100) or ((PATH_PROBE_SCENARIO == 58 or PATH_PROBE_SCENARIO == 59) and udg_PathProbeTick == 90) then
        call PathProbeRecord("before_target_teleport")
        if PATH_PROBE_SCENARIO == 56 or PATH_PROBE_SCENARIO == 58 then
            call SetUnitX(udg_PathProbeTarget, -1600.0)
            call SetUnitY(udg_PathProbeTarget, 300.0)
        else
            call SetUnitPosition(udg_PathProbeTarget, -1600.0, 300.0)
        endif
        call PathProbeRecord("after_target_teleport")
    endif
    if (PATH_PROBE_SCENARIO == 60 or PATH_PROBE_SCENARIO == 61 or PATH_PROBE_SCENARIO == 62) and udg_PathProbeTick == 100 then
        call PathProbeRecord("before_target_resize")
        call Preload("PATHTARGET tick=100 label=before_resize type=" + I2S(GetUnitTypeId(udg_PathProbeTarget)) + " handle=" + I2S(GetHandleId(udg_PathProbeTarget)) + " chaosLevel=" + I2S(GetUnitAbilityLevel(udg_PathProbeTarget, 'ACGb')) + " chaosShrinkLevel=" + I2S(GetUnitAbilityLevel(udg_PathProbeTarget, 'ACSh')))
        if PATH_PROBE_SCENARIO == 60 or PATH_PROBE_SCENARIO == 62 then
            if UnitAddAbility(udg_PathProbeTarget, 'ACGb') then
                call PathProbeRecord("target_resize_accepted")
            else
                call PathProbeRecord("target_resize_rejected")
            endif
        else
            if UnitAddAbility(udg_PathProbeTarget, 'ACSh') then
                call PathProbeRecord("target_resize_accepted")
            else
                call PathProbeRecord("target_resize_rejected")
            endif
        endif
        call Preload("PATHTARGET tick=100 label=after_resize type=" + I2S(GetUnitTypeId(udg_PathProbeTarget)) + " handle=" + I2S(GetHandleId(udg_PathProbeTarget)) + " chaosLevel=" + I2S(GetUnitAbilityLevel(udg_PathProbeTarget, 'ACGb')) + " chaosShrinkLevel=" + I2S(GetUnitAbilityLevel(udg_PathProbeTarget, 'ACSh')))
        call PathProbeRecord("after_target_resize")
    endif
    if PATH_PROBE_SCENARIO == 62 and udg_PathProbeTick == 150 then
        call SetPlayerTechResearched(Player(0), 'Roch', 1)
        call PathProbeRecord("target_resize_research")
    endif
    if (PATH_PROBE_SCENARIO == 60 or PATH_PROBE_SCENARIO == 61) and udg_PathProbeTick == 150 then
        call PathProbeRecord("before_resized_follow")
        if IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget) then
            call PathProbeRecord("resized_follow_accepted")
        else
            call PathProbeRecord("resized_follow_rejected")
        endif
        call PathProbeRecord("after_resized_follow")
    endif
    if (PATH_PROBE_SCENARIO == 60 or PATH_PROBE_SCENARIO == 61) and udg_PathProbeTick == 150 then
        call PathProbeRecord("before_resized_follow")
        if IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget) then
            call PathProbeRecord("resized_follow_accepted")
        else
            call PathProbeRecord("resized_follow_rejected")
        endif
        call PathProbeRecord("after_resized_follow")
    endif
    if (PATH_PROBE_SCENARIO == 54 or PATH_PROBE_SCENARIO == 55) then
        if udg_PathProbeTick == 100 then
            call PathProbeRecord("before_target_retirement")
            call Preload("PATHTARGET tick=100 handleBefore=" + I2S(GetHandleId(udg_PathProbeTarget)))
            if PATH_PROBE_SCENARIO == 54 then
                call RemoveUnit(udg_PathProbeTarget)
                set udg_PathProbeTarget = null
            else
                call KillUnit(udg_PathProbeTarget)
            endif
            call PathProbeRecord("after_target_retirement")
        elseif udg_PathProbeTick == 105 then
            if PATH_PROBE_SCENARIO == 55 then
                call RemoveUnit(udg_PathProbeTarget)
                set udg_PathProbeTarget = null
            endif
        elseif udg_PathProbeTick == 110 then
            set udg_PathProbeTarget = CreateUnit(Player(0), 'hfoo', -1936.0, 112.0, 90.0)
            call SetUnitMoveSpeed(udg_PathProbeTarget, 300.0)
            call Preload("PATHTARGET tick=110 handleAfter=" + I2S(GetHandleId(udg_PathProbeTarget)))
            call PathProbeRecord("replacement_created")
        elseif udg_PathProbeTick == 120 then
            call PathProbeRecord("before_replacement_follow")
            if IssueTargetOrder(udg_PathProbeUnit, "smart", udg_PathProbeTarget) then
                call PathProbeRecord("replacement_follow_accepted")
            else
                call PathProbeRecord("replacement_follow_rejected")
            endif
        endif
    endif
    if (PATH_PROBE_SCENARIO == 12 or (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62)) then
        call Preload("PATHTARGET tick=" + I2S(udg_PathProbeTick) + " x=" + R2S(GetUnitX(udg_PathProbeTarget)) + " y=" + R2S(GetUnitY(udg_PathProbeTarget)) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeTarget)) + " type=" + I2S(GetUnitTypeId(udg_PathProbeTarget)) + " handle=" + I2S(GetHandleId(udg_PathProbeTarget)))
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
    if PATH_PROBE_SCENARIO == 49 or PATH_PROBE_SCENARIO == 50 or (PATH_PROBE_SCENARIO == 51 or PATH_PROBE_SCENARIO == 52) then
        call Preload("PATHSELECT tick=" + I2S(udg_PathProbeTick) + " peerX=" + R2S(GetUnitX(udg_PathProbeCrowd[1])) + " peerY=" + R2S(GetUnitY(udg_PathProbeCrowd[1])) + " peerOrder=" + I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[1])))
    endif
    if PATH_PROBE_SCENARIO>=65 and PATH_PROBE_SCENARIO<=67 then
        if udg_PathProbeTick==20 then
            call PathProbeRecord("before_group_radius_change")
            if PATH_PROBE_SCENARIO==65 then
                call UnitAddAbility(udg_PathProbeCrowd[1],'ACGb')
            elseif PATH_PROBE_SCENARIO==66 then
                call UnitAddAbility(udg_PathProbeCrowd[1],'ACSh')
            else
                call RemoveUnit(udg_PathProbeCrowd[1])
            endif
            call PathProbeRecord("after_group_radius_change")
        endif
        call Preload("PATHGROUPRADIUS tick="+I2S(udg_PathProbeTick)+" peerType="+I2S(GetUnitTypeId(udg_PathProbeCrowd[1]))+" peerHandle="+I2S(GetHandleId(udg_PathProbeCrowd[1]))+" peerOrder="+I2S(GetUnitCurrentOrder(udg_PathProbeCrowd[1]))+" peerX="+R2S(GetUnitX(udg_PathProbeCrowd[1]))+" peerY="+R2S(GetUnitY(udg_PathProbeCrowd[1]))+" grow="+I2S(GetUnitAbilityLevel(udg_PathProbeCrowd[1],'ACGb'))+" shrink="+I2S(GetUnitAbilityLevel(udg_PathProbeCrowd[1],'ACSh')))
    endif
    if PATH_PROBE_SCENARIO == 63 then
        if udg_PathProbeTick == 20 then
            call PathProbeRecord("before_mover_resize")
            call Preload("PATHMORPH tick=20 label=before type=" + I2S(GetUnitTypeId(udg_PathProbeUnit)) + " handle=" + I2S(GetHandleId(udg_PathProbeUnit)))
            if UnitAddAbility(udg_PathProbeUnit, 'ACGb') then
                call PathProbeRecord("mover_resize_accepted")
            else
                call PathProbeRecord("mover_resize_rejected")
            endif
            call Preload("PATHMORPH tick=20 label=after type=" + I2S(GetUnitTypeId(udg_PathProbeUnit)) + " handle=" + I2S(GetHandleId(udg_PathProbeUnit)))
            call PathProbeRecord("after_mover_resize")
        endif
        call Preload("PATHMORPH tick=" + I2S(udg_PathProbeTick) + " type=" + I2S(GetUnitTypeId(udg_PathProbeUnit)) + " handle=" + I2S(GetHandleId(udg_PathProbeUnit)) + " chaos=" + I2S(GetUnitAbilityLevel(udg_PathProbeUnit, 'ACGb')))
    endif
    if PATH_PROBE_SCENARIO==64 then
        if ModuloInteger(udg_PathProbeTick,100)==0 and udg_PathProbeTick<900 then
            call PathProbeRecord("before_matrix_birth")
            call RemoveUnit(udg_PathProbeUnit)
            set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',-1936.0,-976.0,90.0)
            call SetUnitMoveSpeed(udg_PathProbeUnit,100.0)
            set udg_MorphCase=udg_MorphCase+1
            call PathProbeRecord("after_matrix_birth")
            if udg_MorphCase==0 then
                set udg_MorphAbility='Ag01'
            elseif udg_MorphCase==1 then
                set udg_MorphAbility='Ag02'
            elseif udg_MorphCase==2 then
                set udg_MorphAbility='Ag03'
            elseif udg_MorphCase==3 then
                set udg_MorphAbility='Ag04'
            elseif udg_MorphCase==4 then
                set udg_MorphAbility='Ag05'
            elseif udg_MorphCase==5 then
                set udg_MorphAbility='Ag06'
            elseif udg_MorphCase==6 then
                set udg_MorphAbility='Ag07'
            elseif udg_MorphCase==7 then
                set udg_MorphAbility='Ag08'
            elseif udg_MorphCase==8 then
                set udg_MorphAbility='Ag09'
            endif
        endif
        if ModuloInteger(udg_PathProbeTick,100)==10 and udg_PathProbeTick>10 then
            call PathProbeRecord("before_matrix_order")
            if IssuePointOrder(udg_PathProbeUnit,"move",-1936.0,-144.0) then
                call PathProbeRecord("matrix_order_accepted")
            else
                call PathProbeRecord("matrix_order_rejected")
            endif
        endif
        if ModuloInteger(udg_PathProbeTick,100)==20 then
            call PathProbeRecord("before_mover_resize")
            call Preload("PATHMORPH case="+I2S(udg_MorphCase)+" tick="+I2S(udg_PathProbeTick)+" label=before type="+I2S(GetUnitTypeId(udg_PathProbeUnit))+" handle="+I2S(GetHandleId(udg_PathProbeUnit)))
            if UnitAddAbility(udg_PathProbeUnit,udg_MorphAbility) then
                call PathProbeRecord("mover_resize_accepted")
            else
                call PathProbeRecord("mover_resize_rejected")
            endif
            call Preload("PATHMORPH case="+I2S(udg_MorphCase)+" tick="+I2S(udg_PathProbeTick)+" label=after type="+I2S(GetUnitTypeId(udg_PathProbeUnit))+" handle="+I2S(GetHandleId(udg_PathProbeUnit)))
            call PathProbeRecord("after_mover_resize")
        endif
        call Preload("PATHMORPH case="+I2S(udg_MorphCase)+" tick="+I2S(udg_PathProbeTick)+" type="+I2S(GetUnitTypeId(udg_PathProbeUnit))+" handle="+I2S(GetHandleId(udg_PathProbeUnit))+" chaos="+I2S(GetUnitAbilityLevel(udg_PathProbeUnit,udg_MorphAbility)))
    endif
    call PathProbeRecord("sample")
    if (udg_PathProbeTick == 300 and PATH_PROBE_SCENARIO!=64) or (udg_PathProbeTick==900 and PATH_PROBE_SCENARIO==64) then
        if (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62) then
            call PathProbeRecord("before_follow_stop")
            if IssueImmediateOrder(udg_PathProbeUnit, "stop") then
                call PathProbeRecord("follow_stop_accepted")
            else
                call PathProbeRecord("follow_stop_rejected")
            endif
        endif
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
    if (PATH_PROBE_SCENARIO == 34 or PATH_PROBE_SCENARIO == 35) then
        set crowdType = 'Hpal'
    endif
    if PATH_PROBE_SCENARIO == 45 then
        // A small-coordinate source lies in an isolated21-cell footprint component.
        // Clear an explicit24x36-cell control corridor for its numerical journey.
        set gx = 0
        loop
            exitwhen gx == 24
            set gy = 0
            loop
                exitwhen gy == 36
                call SetTerrainPathable(-128.0 + I2R(gx)*32.0, -128.0 + I2R(gy)*32.0, PATHING_TYPE_WALKABILITY, true)
                set gy = gy + 1
            endloop
            set gx = gx + 1
        endloop
        set gx = 0
        set gy = 0
    endif
    if PATH_PROBE_SCENARIO == 47 then
        call Preload("PATHPOSE case=pair_first")
    endif
    set udg_PathProbeUnit = CreateUnit(Player(0), crowdType, -1936.0, -976.0, 90.0)
    if PATH_PROBE_SCENARIO == 47 then
        call Preload("PATHPOSE done=pair_first")
        call Preload("PATHPOSE case=pair_second")
        set udg_PathProbeCrowd[0] = udg_PathProbeUnit
        set udg_PathProbeCrowd[1] = CreateUnit(Player(0), crowdType, -1856.0, -976.0, 90.0)
        call SetUnitMoveSpeed(udg_PathProbeCrowd[1], 350.0)
        call Preload("PATHPOSE done=pair_second")
        set udg_PathProbeGroup = CreateGroup()
        call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[0])
        call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[1])
    endif
    if PATH_PROBE_SCENARIO>=65 and PATH_PROBE_SCENARIO<=67 then
        set udg_PathProbeCrowd[0]=udg_PathProbeUnit
        if PATH_PROBE_SCENARIO==65 then
            set udg_PathProbeCrowd[1]=CreateUnit(Player(0),'hfoo',-1840.0,-976.0,90.0)
        else
            set udg_PathProbeCrowd[1]=CreateUnit(Player(0),'hCLG',-1840.0,-976.0,90.0)
        endif
        call SetUnitMoveSpeed(udg_PathProbeCrowd[1],100.0)
        set udg_PathProbeGroup=CreateGroup()
        call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeCrowd[0])
        call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeCrowd[1])
    endif
    if PATH_PROBE_SCENARIO == 48 then
        set udg_PathProbeCrowd[0] = udg_PathProbeUnit
        set udg_PathProbeGroup = CreateGroup()
        call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeUnit)
        set gx = 1
        loop
            exitwhen gx == 12
            set udg_PathProbeCrowd[gx] = CreateUnit(Player(0), crowdType, -1936.0 + I2R(ModuloInteger(gx, 4)) * 80.0, -976.0 + I2R(gx / 4) * 80.0, 90.0)
            call SetUnitMoveSpeed(udg_PathProbeCrowd[gx], 100.0 + I2R(ModuloInteger(gx, 3)) * 100.0)
            call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[gx])
            set gx = gx + 1
        endloop
    endif
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
    if ((PATH_PROBE_SCENARIO >= 10 and PATH_PROBE_SCENARIO <= 15) or (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62)) or PATH_PROBE_SCENARIO == 22 then
        if PATH_PROBE_SCENARIO >= 13 and PATH_PROBE_SCENARIO <= 15 then
            call SetPlayerAlliance(Player(0), Player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE, true)
            call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE), Player(0), ALLIANCE_PASSIVE, true)
            set udg_PathProbeTarget = CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE), 'hfoo', -1936.0, -144.0, 90.0)
        else
            set udg_PathProbeTarget = CreateUnit(Player(0), 'hfoo', -1936.0, -144.0, 90.0)
        endif
        if (PATH_PROBE_SCENARIO == 12 or (PATH_PROBE_SCENARIO >= 53 and PATH_PROBE_SCENARIO <= 62)) then
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
    if PATH_PROBE_SCENARIO == 46 then
        set udg_PathProbeGroup = CreateGroup()
        set gx = 0
        loop
            exitwhen gx == 14
            if gx == 0 then
                set udg_PathProbeCrowd[gx] = udg_PathProbeUnit
            else
                set udg_PathProbeCrowd[gx] = CreateUnit(Player(0), 'hfoo', -2016.0 + I2R(ModuloInteger(gx, 4))*80.0, -1136.0 + I2R(gx / 4)*80.0, 90.0)
            endif
            call SetUnitMoveSpeed(udg_PathProbeCrowd[gx], 150.0 + I2R(ModuloInteger(gx, 3))*100.0)
            call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[gx])
            set gx = gx + 1
        endloop
        set gx = 0
    endif
    if PATH_PROBE_SCENARIO == 31 then
        set udg_PathProbeCrowd[0] = CreateUnit(Player(0), 'hkni', -2304.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[1] = CreateUnit(Player(0), 'hgry', -2048.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[2] = CreateUnit(Player(0), 'hsor', -1792.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[3] = CreateUnit(Player(0), 'hbot', -1536.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[4] = CreateUnit(Player(0), 'uplg', -1280.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[5] = CreateUnit(Player(0), 'halt', -1024.0, -1216.0, 90.0)
        call Preload("PATHTRACE tick=0 label=profiles_created x=" + R2S(GetUnitX(udg_PathProbeUnit)) + " y=" + R2S(GetUnitY(udg_PathProbeUnit)) + " order=" + I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
    endif
    if PATH_PROBE_SCENARIO == 32 then
        set udg_PathProbeCrowd[0] = CreateUnit(Player(0), 'h001', -2304.0, -1216.0, 90.0)
        set udg_PathProbeCrowd[1] = CreateUnit(Player(0), 'halt', -2560.0, -1216.0, 90.0)
    elseif (PATH_PROBE_SCENARIO == 34 or PATH_PROBE_SCENARIO == 35) then
        set udg_PathProbeBootsOne = CreateItem('bspd', -2240.0, -976.0)
        set udg_PathProbeBootsTwo = CreateItem('bspd', -2304.0, -976.0)
    else
        call SetUnitMoveSpeed(udg_PathProbeUnit, 100.0)
    endif
    call FogEnable(PATH_PROBE_SCENARIO == 14 or PATH_PROBE_SCENARIO == 15)
    call FogMaskEnable(false)
    call SetCameraPosition(-1936.0, -560.0)
    call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE, 1800.0, 0.0)
    if PATH_PROBE_SCENARIO == 49 or PATH_PROBE_SCENARIO == 50 or (PATH_PROBE_SCENARIO == 51 or PATH_PROBE_SCENARIO == 52) then
        call SetPlayerController(GetLocalPlayer(), MAP_CONTROL_USER)
        call SetUnitOwner(udg_PathProbeUnit, GetLocalPlayer(), false)
        set udg_PathProbeCrowd[0] = udg_PathProbeUnit
        set udg_PathProbeCrowd[1] = CreateUnit(GetLocalPlayer(), 'hfoo', -1856.0, -976.0, 90.0)
        call SetUnitMoveSpeed(udg_PathProbeCrowd[1], 100.0)
        call EnableUserControl(true)
        call ShowInterface(true, 0.0)
        call ClearSelection()
        call SelectUnit(udg_PathProbeCrowd[1], true)
        if PATH_PROBE_SCENARIO == 50 or (PATH_PROBE_SCENARIO == 51 or PATH_PROBE_SCENARIO == 52) then
            set udg_PathProbeGroup = CreateGroup()
            call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[0])
            if PATH_PROBE_SCENARIO == 52 then
                set udg_PathProbePeerGroup = CreateGroup()
                call GroupAddUnit(udg_PathProbePeerGroup, udg_PathProbeCrowd[1])
            endif
            if PATH_PROBE_SCENARIO == 50 then
                call GroupAddUnit(udg_PathProbeGroup, udg_PathProbeCrowd[1])
            endif
        endif
    endif
    call SelectUnit(udg_PathProbeUnit, true)
    if PATH_PROBE_SCENARIO == 68 then
        call SetUnitMoveSpeed(udg_PathProbeUnit,522.0)
    endif
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
    if PATH_PROBE_SCENARIO == 1 or PATH_PROBE_SCENARIO == 3 or PATH_PROBE_SCENARIO == 4 or PATH_PROBE_SCENARIO == 41 or PATH_PROBE_SCENARIO == 42 or PATH_PROBE_SCENARIO == 43 then
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
