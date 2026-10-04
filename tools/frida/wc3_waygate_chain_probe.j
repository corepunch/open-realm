globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbeGateA=null
 unit udg_PathProbeGateB=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeStep takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,768.0,0.0)
  set udg_PathProbeGateB=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',1152.0,1280.0,0.0)
  call WaygateSetDestination(udg_PathProbeGateA,1088.0,1216.0)
  call WaygateSetDestination(udg_PathProbeGateB,1728.0,1760.0)
  call WaygateActivate(udg_PathProbeGateA,true)
  call WaygateActivate(udg_PathProbeGateB,true)
 elseif udg_PathProbeTick==2 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeUnit,0.0)
 elseif udg_PathProbeTick==3 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
 elseif udg_PathProbeTick==160 or udg_PathProbeTick==320 or udg_PathProbeTick==480 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitPosition(udg_PathProbeUnit,272.0,304.0)
  call WaygateActivate(udg_PathProbeGateA,udg_PathProbeTick==320)
  call WaygateActivate(udg_PathProbeGateB,udg_PathProbeTick==160)
  call IssuePointOrder(udg_PathProbeUnit,"move",1744.0,1776.0)
  call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=gate_chain_next case=99")
 elseif udg_PathProbeTick==660 then
  call Preload("PATHTRACE tick=660 label=gate_chain_complete case=99")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=99")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.05,true,function PathProbeStep)
endfunction
