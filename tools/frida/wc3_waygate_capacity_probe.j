globals
 unit udg_PathProbeUnit=null
 unit array udg_PathProbeGate
 integer udg_PathProbeIndex=0
 integer udg_PathProbeTick=0
endglobals
function PathProbeStep takes nothing returns nothing
 local integer i=0
 local unit g=null
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  call Preload("PFSCENE start_movement_bypasses gate_capacity")
 endif
 if udg_PathProbeIndex<256 then
  loop
   exitwhen i==4 or udg_PathProbeIndex==256
   set g=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',64.0+I2R(ModuloInteger(udg_PathProbeIndex,16))*112.0,64.0+I2R(udg_PathProbeIndex/16)*112.0,0.0)
   set udg_PathProbeGate[udg_PathProbeIndex]=g
   call WaygateSetDestination(g,768.0,768.0)
   call WaygateActivate(g,true)
   if WaygateIsActive(g) then
    call Preload("PFSCENE gate_pool "+I2S(udg_PathProbeIndex)+" active=1")
   else
    call Preload("PFSCENE gate_pool "+I2S(udg_PathProbeIndex)+" active=0")
   endif
   set udg_PathProbeIndex=udg_PathProbeIndex+1
   set i=i+1
  endloop
 elseif udg_PathProbeTick==70 then
  call RemoveUnit(udg_PathProbeGate[16])
  call RemoveUnit(udg_PathProbeGate[254])
  call Preload("PFSCENE gate_pool_freed")
 elseif udg_PathProbeTick==72 then
  set g=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',64.0,64.0,0.0)
  call WaygateActivate(g,true)
  set g=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',192.0,64.0,0.0)
  call WaygateActivate(g,true)
  set g=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',320.0,64.0,0.0)
  call WaygateActivate(g,true)
  call Preload("PFSCENE gate_pool_complete")
  call DestroyTimer(GetExpiredTimer())
 endif
 set g=null
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHTRACE tick=0 label=start_movement_bypasses case=0")
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.10,true,function PathProbeStep)
endfunction
