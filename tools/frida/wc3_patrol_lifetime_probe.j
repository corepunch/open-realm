globals
 unit array udg_PathPatrolSubjects
 unit array udg_PathPatrolEnemies
 integer array udg_PathPatrolDamageSource
 boolean udg_PathPatrolNestedDone=false
 unit udg_PathPatrolDead=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathPatrolRow=0
 hashtable udg_PathPatrolTable=null
 trigger udg_PathPatrolDamageTrigger=null
endglobals
function PathPatrolRecord takes integer i, string label returns nothing
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,1,i)
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,2,GetUnitCurrentOrder(udg_PathPatrolSubjects[i]))
 call SaveReal(udg_PathPatrolTable,udg_PathPatrolRow,3,GetUnitState(udg_PathPatrolEnemies[i],UNIT_STATE_LIFE))
 call SaveReal(udg_PathPatrolTable,udg_PathPatrolRow,4,GetUnitX(udg_PathPatrolSubjects[i]))
 call SaveReal(udg_PathPatrolTable,udg_PathPatrolRow,5,GetUnitY(udg_PathPatrolSubjects[i]))
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,6,GetHandleId(udg_PathPatrolSubjects[i]))
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,7,GetHandleId(udg_PathPatrolEnemies[i]))
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,8,udg_PathPatrolDamageSource[i])
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,9,GetUnitTypeId(udg_PathPatrolSubjects[i]))
 call Preload("PATHMETA patrol_lifetime row="+I2S(udg_PathPatrolRow)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label)
 set udg_PathPatrolRow=udg_PathPatrolRow+1
endfunction
function PathPatrolDamage takes nothing returns nothing
 local integer i=1
 loop
  exitwhen i==5
  if GetTriggerUnit()==udg_PathPatrolEnemies[i] then
   set udg_PathPatrolDamageSource[i]=GetHandleId(GetEventDamageSource())
   if i==3 and not udg_PathPatrolNestedDone then
    set udg_PathPatrolNestedDone=true
    call PathPatrolRecord(i,"nested_before")
    call IssuePointOrder(udg_PathPatrolSubjects[i],"move",1728.0,1024.0)
    call RemoveUnit(udg_PathPatrolEnemies[i])
    call PathPatrolRecord(i,"nested_after")
   endif
  endif
  set i=i+1
 endloop
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 local boolean accepted=false
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==7
  if udg_PathProbeTick==1 then
   set accepted=IssuePointOrder(udg_PathPatrolSubjects[i],"patrol",1008.0,256.0+I2R(i)*256.0)
   call PathPatrolRecord(i,"patrol")
  elseif udg_PathProbeTick==10 and i>=1 and i<=4 then
   set udg_PathPatrolEnemies[i]=CreateUnit(Player(1),'hfoo',GetUnitX(udg_PathPatrolSubjects[i])+48.0,256.0+I2R(i)*256.0,180.0)
   call SetUnitState(udg_PathPatrolEnemies[i],UNIT_STATE_MAX_LIFE,10000.0)
   call SetUnitState(udg_PathPatrolEnemies[i],UNIT_STATE_LIFE,10000.0)
   call PauseUnit(udg_PathPatrolEnemies[i],true)
   call TriggerRegisterUnitEvent(udg_PathPatrolDamageTrigger,udg_PathPatrolEnemies[i],EVENT_UNIT_DAMAGED)
  elseif udg_PathProbeTick==30 and i==4 then
   call PathPatrolRecord(i,"reject_before")
   set accepted=IssueTargetOrder(udg_PathPatrolSubjects[i],"repair",udg_PathPatrolEnemies[i])
   if accepted then
    call PathPatrolRecord(i,"reject_accepted")
   else
    call PathPatrolRecord(i,"reject_refused")
   endif
  elseif udg_PathProbeTick==45 and (i==1 or i==2 or i==4) then
   call PathPatrolRecord(i,"loss_before")
   if i==2 then
    call RemoveUnit(udg_PathPatrolEnemies[i])
   else
    call KillUnit(udg_PathPatrolEnemies[i])
   endif
   call PathPatrolRecord(i,"loss_after")
  elseif udg_PathProbeTick==70 and i==4 then
   call IssueImmediateOrder(udg_PathPatrolSubjects[i],"stop")
   call PathPatrolRecord(i,"stop")
  elseif udg_PathProbeTick==80 and i==2 then
   call RemoveUnit(udg_PathPatrolSubjects[i])
   call PathPatrolRecord(i,"removed")
  elseif udg_PathProbeTick==81 and i==2 then
   set udg_PathPatrolSubjects[i]=CreateUnit(Player(0),'hfoo',272.0,768.0,0.0)
   call PathPatrolRecord(i,"replacement")
  endif
  call PathPatrolRecord(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=patrol_lifetime")
 if udg_PathProbeTick==240 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 local integer x=0
 local integer y=0
 call Preload("PATHMETA case=metadata_patrol_lifetime")
 set udg_PathPatrolTable=InitHashtable()
 set udg_PathPatrolDamageTrigger=CreateTrigger()
 call TriggerAddAction(udg_PathPatrolDamageTrigger,function PathPatrolDamage)
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 loop
  exitwhen x==5
  set y=0
  loop
   exitwhen y==5
   call SetTerrainPathable(944.0+I2R(x)*32.0,1472.0+I2R(y)*32.0,PATHING_TYPE_WALKABILITY,false)
   set y=y+1
  endloop
  set x=x+1
 endloop
 loop
  exitwhen i==7
  set udg_PathPatrolSubjects[i]=CreateUnit(Player(0),'hfoo',272.0,256.0+I2R(i)*256.0,0.0)
  call SetUnitAcquireRange(udg_PathPatrolSubjects[i],128.0)
  call PathPatrolRecord(i,"initial")
  set i=i+1
 endloop
 set udg_PathPatrolDead=CreateUnit(Player(1),'hfoo',1728.0,1280.0,0.0)
 call KillUnit(udg_PathPatrolDead)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1008.0)
 call Preload("PATHTRACE tick=0 label=start_patrol_lifetime")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
