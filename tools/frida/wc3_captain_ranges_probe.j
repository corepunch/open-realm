// Authored weapon boundaries, independent of current slot eligibility.
globals
    unit udg_PathProbeUnit=null
    unit array udg_PathProbeCrowd
    timer udg_PathProbeTimer=null
    integer udg_PathProbeTick=0
endglobals
function PathProbeTick takes nothing returns nothing
    set udg_PathProbeTick=udg_PathProbeTick+1
    if udg_PathProbeTick==10 then
        call Preload("PATHRANGE initial recruit")
        call StartCampaignAI(Player(0),"Scripts\\wc3_captain_probe.ai")
    elseif udg_PathProbeTick==50 then
        call Preload("PATHRANGE remove missile above threshold")
        call RemoveUnit(udg_PathProbeCrowd[2])
        call RemoveUnit(udg_PathProbeCrowd[4])
    elseif udg_PathProbeTick==60 then
        call Preload("PATHRANGE before replacement")
        set udg_PathProbeCrowd[6]=CreateUnit(Player(0),'hCR5',-1744.0,-1168.0,90.0)
        call SetUnitMoveSpeed(udg_PathProbeCrowd[6],100.0)
    elseif udg_PathProbeTick==100 then
        call Preload("PATHMETA complete")
        call PauseTimer(udg_PathProbeTimer)
    endif
    call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=sample x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    call FogEnable(false)
    call FogMaskEnable(false)
    call Preload("PATHTRACE tick=0 label=start_captain_ranges x=0 y=0 order=0")
    loop
        exitwhen i==6
        set udg_PathProbeCrowd[i]=CreateUnit(Player(0),'hCR0'+i,-1936.0+I2R(ModuloInteger(i,3))*96.0,-976.0-I2R(i/3)*96.0,90.0)
        call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)
        set i=i+1
    endloop
    set udg_PathProbeUnit=udg_PathProbeCrowd[0]
    call SetCameraPosition(-1856.0,-512.0)
    set udg_PathProbeTimer=CreateTimer()
    call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
