globals
 unit udg_PathProbeUnit=null
 unit udg_PathProbeGateA=null
 unit udg_PathProbeGateB=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeStep takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,512.0,0.0)
 elseif udg_PathProbeTick==2 then
  set udg_PathProbeGateB=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',768.0,768.0,0.0)
 elseif udg_PathProbeTick==3 then
  call RemoveUnit(udg_PathProbeGateA)
 elseif udg_PathProbeTick==4 then
  call RemoveUnit(udg_PathProbeGateB)
 elseif udg_PathProbeTick==5 then
  set udg_PathProbeGateB=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',768.0,768.0,0.0)
 elseif udg_PathProbeTick==6 then
  set udg_PathProbeGateA=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,512.0,0.0)
 elseif udg_PathProbeTick==7 then
  call RemoveUnit(udg_PathProbeGateB)
 elseif udg_PathProbeTick==8 then
  call RemoveUnit(udg_PathProbeGateA)
 elseif udg_PathProbeTick==9 then
  call Preload("PATHTRACE tick=9 label=gate_overlap_complete case=94")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=94")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.10,true,function PathProbeStep)
endfunction
