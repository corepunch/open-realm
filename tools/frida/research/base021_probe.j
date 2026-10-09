globals
    unit array udg_B021Unit
    integer udg_B021Count=0
    timer udg_PathProbeTimer=null
    integer udg_PathProbeTick=0
    location udg_B021Loc=null
    unit udg_PathProbeUnit=null
endglobals

// BASE-02.1 movement-type producer probe. Public natives only; markers are the
// observer-free contract (PATHTRACE/PATHSTOCK), compared by control_wc3_pathfinding.py.
function B021Add takes player p, integer id, real x, real y returns nothing
    set udg_B021Unit[udg_B021Count]=CreateUnit(p,id,x,y,90.0)
    call SetUnitAcquireRange(udg_B021Unit[udg_B021Count],0.0)
    set udg_B021Count=udg_B021Count+1
endfunction

function B021Bool takes boolean b returns string
    if b then
        return "1"
    endif
    return "0"
endfunction

function B021Snapshot takes string label returns nothing
    local integer i=0
    local unit u
    loop
        exitwhen i>=udg_B021Count
        set u=udg_B021Unit[i]
        call MoveLocation(udg_B021Loc,GetUnitX(u),GetUnitY(u))
        call Preload("PATHSTOCK tick="+I2S(udg_PathProbeTick)+" label="+label+" i="+I2S(i)+" type="+I2S(GetUnitTypeId(u))+" x="+R2S(GetUnitX(u))+" y="+R2S(GetUnitY(u))+" fly="+R2S(GetUnitFlyHeight(u))+" z="+R2S(GetLocationZ(udg_B021Loc))+" air="+B021Bool(IsUnitType(u,UNIT_TYPE_FLYING))+" ground="+B021Bool(IsUnitType(u,UNIT_TYPE_GROUND))+" order="+I2S(GetUnitCurrentOrder(u))+" amrf="+I2S(GetUnitAbilityLevel(u,'Amrf'))+" ens="+I2S(GetUnitAbilityLevel(u,'Bena'))+"/"+I2S(GetUnitAbilityLevel(u,'Beng')))
        set i=i+1
    endloop
    set u=null
endfunction

function PathProbeRecord takes string label returns nothing
    call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction

function PathProbeTick takes nothing returns nothing
    set udg_PathProbeTick=udg_PathProbeTick+1
    if udg_PathProbeTick==10 then
        // Amrf add/remove (index 15), then fly-height writes on Amrf/control/hover/flyer.
        call UnitAddAbility(udg_B021Unit[15],'Amrf')
        call UnitRemoveAbility(udg_B021Unit[15],'Amrf')
        call SetUnitFlyHeight(udg_B021Unit[15],200.0,0.0)
        call SetUnitFlyHeight(udg_B021Unit[16],200.0,0.0)
        call SetUnitFlyHeight(udg_B021Unit[17],200.0,0.0)
        call SetUnitFlyHeight(udg_B021Unit[23],300.0,0.0)
        call PathProbeRecord("fly_heights")
    elseif udg_PathProbeTick==30 then
        call UnitAddAbility(udg_B021Unit[18],'Amrf')
        call PathProbeRecord("amrf_added")
    elseif udg_PathProbeTick==40 then
        call Preload("PATHSTOCK tick=40 label=orders raven18="+B021Bool(IssueImmediateOrder(udg_B021Unit[18],"ravenform"))+" raven19="+B021Bool(IssueImmediateOrder(udg_B021Unit[19],"ravenform"))+" burrow22="+B021Bool(IssueImmediateOrder(udg_B021Unit[22],"burrow")))
        call PathProbeRecord("morphs_requested")
    elseif udg_PathProbeTick==60 then
        call Preload("PATHSTOCK tick=60 label=ensnare issued="+B021Bool(IssueTargetOrder(udg_B021Unit[20],"ensnare",udg_B021Unit[21])))
        call PathProbeRecord("ensnare_requested")
    elseif udg_PathProbeTick==120 then
        call Preload("PATHSTOCK tick=120 label=unorders raven18="+B021Bool(IssueImmediateOrder(udg_B021Unit[18],"unravenform"))+" raven19="+B021Bool(IssueImmediateOrder(udg_B021Unit[19],"unravenform"))+" burrow22="+B021Bool(IssueImmediateOrder(udg_B021Unit[22],"unburrow")))
        call PathProbeRecord("unmorphs_requested")
    endif
    if udg_PathProbeTick==1 or udg_PathProbeTick==12 or udg_PathProbeTick==35 or udg_PathProbeTick==55 or udg_PathProbeTick==65 or udg_PathProbeTick==80 or udg_PathProbeTick==110 or udg_PathProbeTick==150 or udg_PathProbeTick==200 or udg_PathProbeTick==290 then
        call B021Snapshot("state")
    endif
    call PathProbeRecord("sample")
    if udg_PathProbeTick==300 then
        call PathProbeRecord("complete")
        call PreloadGenEnd("pathtrace-base021_types.txt")
        call PauseTimer(udg_PathProbeTimer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    local integer i=0
    local integer array ids
    call PreloadGenClear()
    call PreloadGenStart()
    set udg_B021Loc=Location(0.0,0.0)
    call SetPlayerTechResearched(Player(0),'Roen',1)
    call SetPlayerTechResearched(Player(0),'Rubu',1)
    call SetPlayerTechResearched(Player(0),'Redt',2)
    call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
    call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
    set ids[0]='hM00'
    set ids[1]='hM01'
    set ids[2]='hM02'
    set ids[3]='hM03'
    set ids[4]='hM04'
    set ids[5]='hM05'
    set ids[6]='hM06'
    set ids[7]='hM07'
    set ids[8]='hM08'
    set ids[9]='hM09'
    set ids[10]='hM10'
    set ids[11]='hM11'
    set ids[12]='hM12'
    set ids[13]='hM13'
    set ids[14]='hM14'
    loop
        exitwhen i>14
        call B021Add(Player(0),ids[i],-2432.0+I2R(ModuloInteger(i,8))*160.0,-1344.0+I2R(i/8)*160.0)
        set i=i+1
    endloop
    call B021Add(Player(0),'hfoo',-2432.0,-1024.0)
    call B021Add(Player(0),'hfoo',-2272.0,-1024.0)
    call B021Add(Player(0),'hsor',-2112.0,-1024.0)
    call B021Add(Player(0),'hfoo',-1952.0,-1024.0)
    call B021Add(Player(0),'edot',-1792.0,-1024.0)
    call B021Add(Player(0),'orai',-1504.0,-864.0)
    call B021Add(Player(1),'hgry',-1344.0,-864.0)
    call B021Add(Player(0),'ucry',-1632.0,-1024.0)
    call B021Add(Player(0),'hgry',-1952.0,-864.0)
    call SetUnitState(udg_B021Unit[19],UNIT_STATE_MANA,200.0)
    call SetUnitInvulnerable(udg_B021Unit[20],true)
    set udg_PathProbeUnit=udg_B021Unit[0]
    call FogEnable(false)
    call FogMaskEnable(false)
    call SetCameraPosition(-1952.0,-1100.0)
    call PathProbeRecord("start_base021_types")
    call B021Snapshot("created")
    set udg_PathProbeTimer=CreateTimer()
    call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
