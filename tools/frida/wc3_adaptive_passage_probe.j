globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call PathProbeRecord("before_move")
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
  call PathProbeRecord("after_move")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,100.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_adaptive_passage")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
