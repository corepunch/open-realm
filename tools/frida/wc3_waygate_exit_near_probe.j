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
  call SetTerrainPathable(1744.0,1776.0,PATHING_TYPE_WALKABILITY,false)
  call Preload("PATHTRACE tick=10 label=gate_block_cached_exit case=97")
 elseif udg_PathProbeTick==120 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetTerrainPathable(1744.0,1776.0,PATHING_TYPE_WALKABILITY,true)
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call WaygateSetDestination(udg_PathProbeGateA,2752.0,2784.0)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
  call Preload("PATHTRACE tick=120 label=gate_outside_fresh_exit case=97")
 elseif udg_PathProbeTick==280 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call WaygateSetDestination(udg_PathProbeGateA,-512.0,-544.0)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
  call Preload("PATHTRACE tick=280 label=gate_negative_fresh_exit case=97")
 elseif udg_PathProbeTick==500 then
  call Preload("PATHTRACE tick=500 label=gate_exit_complete case=97")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=97")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
