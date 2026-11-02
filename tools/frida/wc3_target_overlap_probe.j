globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbeTarget=null
 unit udg_PathProbeBlocker=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeObjects takes string label returns nothing
 call Preload("PATHOVERLAP tick="+I2S(udg_PathProbeTick)+" label="+label+" targetX="+R2S(GetUnitX(udg_PathProbeTarget))+" targetY="+R2S(GetUnitY(udg_PathProbeTarget))+" blockerX="+R2S(GetUnitX(udg_PathProbeBlocker))+" blockerY="+R2S(GetUnitY(udg_PathProbeBlocker)))
 call PathProbeRecord(label)
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call PathProbeObjects("before_blocker_latest_order")
  call IssueTargetOrder(udg_PathProbeUnit,"smart",udg_PathProbeTarget)
  call PathProbeObjects("after_blocker_latest_order")
 elseif udg_PathProbeTick==60 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitX(udg_PathProbeUnit,272.0)
  call SetUnitY(udg_PathProbeUnit,304.0)
  call SetUnitX(udg_PathProbeTarget,1136.0)
  call SetUnitX(udg_PathProbeTarget,1008.0)
  call PathProbeObjects("target_reinserted")
 elseif udg_PathProbeTick==70 then
  call PathProbeObjects("before_target_latest_order")
  call IssueTargetOrder(udg_PathProbeUnit,"smart",udg_PathProbeTarget)
  call PathProbeObjects("after_target_latest_order")
 elseif udg_PathProbeTick==120 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitX(udg_PathProbeUnit,272.0)
  call SetUnitY(udg_PathProbeUnit,304.0)
  call RemoveUnit(udg_PathProbeBlocker)
  set udg_PathProbeBlocker=null
  call PathProbeObjects("blocker_removed")
 elseif udg_PathProbeTick==130 then
  call IssueTargetOrder(udg_PathProbeUnit,"smart",udg_PathProbeTarget)
  call PathProbeObjects("after_unobstructed_order")
 elseif udg_PathProbeTick==180 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call PathProbeObjects("final_stop")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 set udg_PathProbeTarget=CreateUnit(Player(0),'hV80',1008.0,1040.0,90.0)
 set udg_PathProbeBlocker=CreateUnit(Player(0),'hV80',1136.0,1040.0,90.0)
 call PauseUnit(udg_PathProbeTarget,true)
 call PauseUnit(udg_PathProbeBlocker,true)
 call SetUnitX(udg_PathProbeBlocker,1008.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,100.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeObjects("start_target_overlap")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
