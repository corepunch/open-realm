globals
 unit array udg_PathRepairWorkers
 unit array udg_PathRepairTargets
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathRepairRow=0
 hashtable udg_PathRepairTable=null
endglobals
function PathRepairQueueRecord takes integer i, string label returns nothing
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,1,i)
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,2,GetUnitCurrentOrder(udg_PathRepairWorkers[i]))
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,3,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE))
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,4,GetUnitX(udg_PathRepairWorkers[i]))
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,5,GetUnitY(udg_PathRepairWorkers[i]))
 call Preload("PATHMETA repair_queue row="+I2S(udg_PathRepairRow)+" tick="+I2S(udg_PathProbeTick)+" worker="+I2S(i)+" label="+label)
 set udg_PathRepairRow=udg_PathRepairRow+1
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 local integer phase=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==4
  set phase=udg_PathProbeTick-i*70
  if phase==1 then
   call ClearSelection()
   call SelectUnit(udg_PathRepairWorkers[i],true)
   call SetCameraPosition(1008.0,320.0+I2R(i)*448.0)
   call SetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_MAX_LIFE)-10.0)
   call IssuePointOrder(udg_PathRepairWorkers[i],"move",1008.0,320.0+I2R(i)*448.0)
   call PathRepairQueueRecord(i,"move")
  endif
  call PathRepairQueueRecord(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=repair_queue")
 if udg_PathProbeTick==300 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call Preload("PATHMETA case=metadata_repair_queue")
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call SetCameraBounds(-1024.0,-1024.0,3072.0,3072.0,-1024.0,3072.0,3072.0,-1024.0)
 set udg_PathRepairTable=InitHashtable()
 call SetPlayerState(GetLocalPlayer(),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(GetLocalPlayer(),PLAYER_STATE_RESOURCE_LUMBER,100000)
 set udg_PathRepairWorkers[0]=CreateUnit(GetLocalPlayer(),'hpea',272.0,320.0,0.0)
 set udg_PathRepairWorkers[1]=CreateUnit(GetLocalPlayer(),'opeo',272.0,768.0,0.0)
 set udg_PathRepairWorkers[2]=CreateUnit(GetLocalPlayer(),'uaco',272.0,1216.0,0.0)
 set udg_PathRepairWorkers[3]=CreateUnit(GetLocalPlayer(),'ewsp',272.0,1664.0,0.0)
 set udg_PathRepairTargets[0]=CreateUnit(GetLocalPlayer(),'hbar',1008.0,320.0,270.0)
 set udg_PathRepairTargets[1]=CreateUnit(GetLocalPlayer(),'obar',1008.0,768.0,270.0)
 set udg_PathRepairTargets[2]=CreateUnit(GetLocalPlayer(),'unpl',1008.0,1216.0,270.0)
 set udg_PathRepairTargets[3]=CreateUnit(GetLocalPlayer(),'etol',1008.0,1664.0,270.0)
 loop
  exitwhen i==4
  call PathRepairQueueRecord(i,"initial")
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,320.0)
 call Preload("PATHTRACE tick=0 label=start_repair_queue")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
