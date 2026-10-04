globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbeGateA=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeStep takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,768.0,0.0)
  call WaygateSetDestination(udg_PathProbeGateA,1728.0,1760.0)
  call WaygateActivate(udg_PathProbeGateA,true)
 elseif udg_PathProbeTick==2 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeUnit,0.0)
 elseif udg_PathProbeTick==3 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==10 then
  call WaygateSetDestination(udg_PathProbeGateA,1216.0,1408.0)
  call Preload("PATHTRACE tick=10 label=gate_retarget_cached case=95")
 elseif udg_PathProbeTick==60 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==180 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==190 then
  call WaygateActivate(udg_PathProbeGateA,false)
  call Preload("PATHTRACE tick=190 label=gate_disable_cached case=95")
 elseif udg_PathProbeTick==400 then
  call Preload("PATHTRACE tick=400 label=gate_traversal_complete case=95")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=95")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
