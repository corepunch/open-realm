globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbePeer=null
 unit udg_PathProbeGateA=null
 group udg_PathProbeGroup=null
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
  set udg_PathProbePeer=CreateUnit(Player(0),'hF91',304.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeUnit,0.0)
  call SetUnitAcquireRange(udg_PathProbePeer,0.0)
  set udg_PathProbeGroup=CreateGroup()
  call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeUnit)
  call GroupAddUnit(udg_PathProbeGroup,udg_PathProbePeer)
 elseif udg_PathProbeTick==3 then
  call GroupPointOrder(udg_PathProbeGroup,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==120 then
  call GroupImmediateOrder(udg_PathProbeGroup,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call SetUnitPosition(udg_PathProbePeer,304.0,304.0)
  call GroupPointOrder(udg_PathProbeGroup,"move",1744.0,1776.0)
  call Preload("PATHTRACE tick=120 label=gate_group_second_approach case=98b")
 elseif udg_PathProbeTick==130 then
  call WaygateActivate(udg_PathProbeGateA,false)
  call Preload("PATHTRACE tick=130 label=gate_group_disabled_cached case=98b")
 elseif udg_PathProbeTick==500 then
  call Preload("PATHTRACE tick=500 label=gate_group_complete case=98b")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=98b")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
