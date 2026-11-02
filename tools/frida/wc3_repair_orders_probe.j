globals
 unit array udg_PathRepairWorkers
 unit array udg_PathRepairTargets
 unit udg_PathRepairOrganic=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathRepairRow=0
 hashtable udg_PathRepairTable=null
endglobals
function PathRepairOrder takes integer i returns string
 if i==2 then
  return "restoration"
 elseif i==3 then
  return "renew"
 endif
 return "repair"
endfunction
function PathRepairRecord takes integer i, string label, boolean accepted returns nothing
 local integer result=0
 if accepted then
  set result=1
 endif
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,1,i)
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,2,GetUnitCurrentOrder(udg_PathRepairWorkers[i]))
 call SaveInteger(udg_PathRepairTable,udg_PathRepairRow,3,result)
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,4,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE))
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,5,GetUnitX(udg_PathRepairWorkers[i]))
 call SaveReal(udg_PathRepairTable,udg_PathRepairRow,6,GetUnitY(udg_PathRepairWorkers[i]))
 call Preload("PATHMETA repair row="+I2S(udg_PathRepairRow)+" tick="+I2S(udg_PathProbeTick)+" worker="+I2S(i)+" label="+label+" accepted="+I2S(result)+" order="+I2S(GetUnitCurrentOrder(udg_PathRepairWorkers[i])))
 set udg_PathRepairRow=udg_PathRepairRow+1
endfunction
function PathRepairDamage takes integer i returns nothing
 call SetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_MAX_LIFE)-100.0)
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 local integer targetType=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==4
  if udg_PathProbeTick==1 or udg_PathProbeTick==30 or udg_PathProbeTick==40 or udg_PathProbeTick==70 then
   call PathRepairDamage(i)
   call PathRepairRecord(i,"repair",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==2 or udg_PathProbeTick==6 then
   call PathRepairRecord(i,"same_target",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==7 then
   call PathRepairRecord(i,"same_smart",IssueTargetOrder(udg_PathRepairWorkers[i],"smart",udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==130 then
   call PathRepairDamage(i)
   call PathRepairRecord(i,"work_smart",IssueTargetOrder(udg_PathRepairWorkers[i],"smart",udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==131 then
   call PathRepairRecord(i,"work_explicit",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==132 then
   call PathRepairRecord(i,"work_smart_repeat",IssueTargetOrder(udg_PathRepairWorkers[i],"smart",udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==3 then
   call PathRepairRecord(i,"invalid",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairOrganic))
  elseif udg_PathProbeTick==4 or udg_PathProbeTick==25 then
   call PathRepairRecord(i,"move",IssuePointOrder(udg_PathRepairWorkers[i],"move",272.0,320.0+I2R(i)*448.0))
  elseif udg_PathProbeTick==5 then
   call PathRepairRecord(i,"smart",IssueTargetOrder(udg_PathRepairWorkers[i],"smart",udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==10 or udg_PathProbeTick==34 then
   call SetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_MAX_LIFE))
   call PathRepairRecord(i,"full",true)
  elseif udg_PathProbeTick==12 then
   call SetUnitPosition(udg_PathRepairWorkers[i],832.0,320.0+I2R(i)*448.0)
   call PathRepairDamage(i)
   call PathRepairRecord(i,"autoon",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"on"))
  elseif udg_PathProbeTick==20 or udg_PathProbeTick==60 then
   call PathRepairRecord(i,"autooff",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"off"))
  elseif udg_PathProbeTick==16 then
   call PathRepairRecord(i,"repeat_on",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"on"))
  elseif udg_PathProbeTick==21 then
   call PathRepairRecord(i,"repeat_off",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"off"))
  elseif udg_PathProbeTick==22 then
   call PathRepairRecord(i,"stop",IssueImmediateOrder(udg_PathRepairWorkers[i],"stop"))
  elseif udg_PathProbeTick==26 then
   call SetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_MAX_LIFE)-1.0)
   call PathRepairRecord(i,"one_hp",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==27 then
   call SetUnitState(udg_PathRepairTargets[i],UNIT_STATE_LIFE,GetUnitState(udg_PathRepairTargets[i],UNIT_STATE_MAX_LIFE)-0.5)
   call PathRepairRecord(i,"subunit_hp",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==44 then
   call RemoveUnit(udg_PathRepairTargets[i])
   call PathRepairRecord(i,"remove_target",true)
  elseif udg_PathProbeTick==50 then
   if i==0 then
    set targetType='hbar'
   elseif i==1 then
    set targetType='obar'
   elseif i==2 then
    set targetType='unpl'
   else
    set targetType='etol'
   endif
   set udg_PathRepairTargets[i]=CreateUnit(Player(0),targetType,1008.0,320.0+I2R(i)*448.0,270.0)
   call PathRepairDamage(i)
   call PathRepairRecord(i,"fresh_target",IssueTargetOrder(udg_PathRepairWorkers[i],PathRepairOrder(i),udg_PathRepairTargets[i]))
  elseif udg_PathProbeTick==55 then
   call KillUnit(udg_PathRepairWorkers[i])
   call PathRepairRecord(i,"death",true)
  elseif udg_PathProbeTick==65 then
   call RemoveUnit(udg_PathRepairWorkers[i])
   if i==0 then
    set targetType='hpea'
   elseif i==1 then
    set targetType='opeo'
   elseif i==2 then
    set targetType='uaco'
   else
    set targetType='ewsp'
   endif
   set udg_PathRepairWorkers[i]=CreateUnit(Player(0),targetType,272.0,320.0+I2R(i)*448.0,0.0)
   call PathRepairRecord(i,"fresh_worker",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"off"))
  endif
  call PathRepairRecord(i,"sample",true)
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=repair")
 if udg_PathProbeTick==300 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call Preload("PATHMETA case=metadata_repair")
 set udg_PathRepairTable=InitHashtable()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 set udg_PathRepairWorkers[0]=CreateUnit(Player(0),'hpea',272.0,320.0,0.0)
 set udg_PathRepairWorkers[1]=CreateUnit(Player(0),'opeo',272.0,768.0,0.0)
 set udg_PathRepairWorkers[2]=CreateUnit(Player(0),'uaco',272.0,1216.0,0.0)
 set udg_PathRepairWorkers[3]=CreateUnit(Player(0),'ewsp',272.0,1664.0,0.0)
 set udg_PathRepairTargets[0]=CreateUnit(Player(0),'hbar',1008.0,320.0,270.0)
 set udg_PathRepairTargets[1]=CreateUnit(Player(0),'obar',1008.0,768.0,270.0)
 set udg_PathRepairTargets[2]=CreateUnit(Player(0),'unpl',1008.0,1216.0,270.0)
 set udg_PathRepairTargets[3]=CreateUnit(Player(0),'etol',1008.0,1664.0,270.0)
 set udg_PathRepairOrganic=CreateUnit(Player(0),'hfoo',1808.0,320.0,0.0)
 loop
  exitwhen i==4
  call PathRepairRecord(i,"initial_off",IssueImmediateOrder(udg_PathRepairWorkers[i],PathRepairOrder(i)+"off"))
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call Preload("PATHTRACE tick=0 label=start_repair")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
