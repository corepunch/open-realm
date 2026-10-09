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
  call RemoveUnit(udg_PathProbeGateA)
  set udg_PathProbeGateA=null
  call Preload("PATHTRACE tick=10 label=gate_destroy_approach case=96")
 elseif udg_PathProbeTick==180 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,768.0,0.0)
  call WaygateSetDestination(udg_PathProbeGateA,1728.0,1760.0)
  call WaygateActivate(udg_PathProbeGateA,true)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==190 then
  call RemoveUnit(udg_PathProbeGateA)
  set udg_PathProbeGateA=null
 elseif udg_PathProbeTick==191 then
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',768.0,512.0,0.0)
  call WaygateSetDestination(udg_PathProbeGateA,1216.0,1408.0)
  call WaygateActivate(udg_PathProbeGateA,true)
  call Preload("PATHTRACE tick=191 label=gate_reuse_cached case=96")
 elseif udg_PathProbeTick==400 then
  call Preload("PATHTRACE tick=400 label=gate_lifetime_complete case=96")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=96")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
