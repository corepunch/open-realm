globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbeFly=null
 unit udg_PathProbeGate=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeStep takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  set udg_PathProbeGate=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,768.0,0.0)
  call WaygateSetDestination(udg_PathProbeGate,1728.0,1760.0)
  call WaygateActivate(udg_PathProbeGate,true)
 elseif udg_PathProbeTick==2 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeUnit,0.0)
 elseif udg_PathProbeTick==3 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==100 then
  call RemoveUnit(udg_PathProbeUnit)
  set udg_PathProbeUnit=null
  set udg_PathProbeFly=CreateUnit(Player(0),'hAIR',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeFly,0.0)
  call Preload("PATHTRACE tick=100 label=gate_created_flight case=100")
 elseif udg_PathProbeTick==101 then
  call IssuePointOrder(udg_PathProbeFly,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==200 then
  call RemoveUnit(udg_PathProbeFly)
  set udg_PathProbeFly=null
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeUnit,0.0)
 elseif udg_PathProbeTick==201 then
  call UnitAddAbility(udg_PathProbeUnit,'ACFl')
  call Preload("PATHTRACE tick=201 label=gate_rebound_flight case=100")
 elseif udg_PathProbeTick==203 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==400 then
  call UnitAddAbility(udg_PathProbeUnit,'ACGr')
  call Preload("PATHTRACE tick=400 label=gate_restored_ground case=100")
 elseif udg_PathProbeTick==401 then
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
 elseif udg_PathProbeTick==403 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==600 then
  call Preload("PATHTRACE tick=600 label=gate_eligibility_complete case=100")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=100")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
