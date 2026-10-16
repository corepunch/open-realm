globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 if IsUnitPaused(udg_PathProbeUnit) then
  call Preload("PATHPAUSEVALUE true")
 else
  call Preload("PATHPAUSEVALUE false")
 endif
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1712.0,1712.0)
  call PathProbeRecord("point_move")
 elseif udg_PathProbeTick==40 or udg_PathProbeTick==150 then
  call SetUnitPathing(udg_PathProbeUnit,false)
  call PathProbeRecord("pathing_off")
 elseif udg_PathProbeTick==85 or udg_PathProbeTick==165 then
  call SetUnitPathing(udg_PathProbeUnit,true)
  call PathProbeRecord("pathing_on")
 elseif udg_PathProbeTick==110 then
  call PauseUnit(udg_PathProbeUnit,true)
  call PathProbeRecord("paused")
 elseif udg_PathProbeTick==115 then
  call SetUnitX(udg_PathProbeUnit,640.25)
  call SetUnitY(udg_PathProbeUnit,608.125)
  call PathProbeRecord("paused_displacement")
 elseif udg_PathProbeTick==135 then
  call PauseUnit(udg_PathProbeUnit,false)
  call PathProbeRecord("resumed")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call Preload("PATHPOSE done=movement_bypasses")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHPOSE case=movement_bypasses")
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_movement_bypasses")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
