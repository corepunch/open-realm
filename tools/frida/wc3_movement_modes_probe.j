globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHMODE type="+I2S(GetUnitTypeId(udg_PathProbeUnit)))
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1712.0,1712.0)
  call PathProbeRecord("point_move")
 elseif udg_PathProbeTick==40 then
  call UnitAddAbility(udg_PathProbeUnit,'ACFl')
  call PathProbeRecord("fly_requested")
 elseif udg_PathProbeTick==85 then
  call SetUnitPosition(udg_PathProbeUnit,272.125,848.25)
  call PathProbeRecord("fly_teleport")
 elseif udg_PathProbeTick==90 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1712.0,1712.0)
  call PathProbeRecord("fly_move")
 elseif udg_PathProbeTick==120 then
  call UnitAddAbility(udg_PathProbeUnit,'ACGr')
  call PathProbeRecord("ground_requested")
 elseif udg_PathProbeTick==150 then
  call SetUnitPosition(udg_PathProbeUnit,336.25,368.125)
  call PathProbeRecord("ground_teleport")
 elseif udg_PathProbeTick==160 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1712.0,1712.0)
  call PathProbeRecord("ground_move")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call Preload("PATHPOSE done=movement_modes")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHPOSE case=movement_modes")
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,200.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_movement_modes")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
