globals
 unit array udg_PathFollowSubjects
 unit array udg_PathFollowTargets
 unit array udg_PathFollowEnemies
 integer array udg_PathFollowDamageSource
 boolean array udg_PathFollowCallback
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathFollowRow=0
 integer udg_PathFollowNestedIndex=0
 hashtable udg_PathFollowTable=null
 trigger udg_PathFollowNested=null
 trigger udg_PathFollowDamageTrigger=null
endglobals
function PathFollowRecord takes integer i, string label returns nothing
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,1,i)
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,2,GetUnitCurrentOrder(udg_PathFollowSubjects[i]))
 call SaveReal(udg_PathFollowTable,udg_PathFollowRow,3,GetUnitState(udg_PathFollowEnemies[i],UNIT_STATE_LIFE))
 call SaveReal(udg_PathFollowTable,udg_PathFollowRow,4,GetUnitX(udg_PathFollowSubjects[i]))
 call SaveReal(udg_PathFollowTable,udg_PathFollowRow,5,GetUnitY(udg_PathFollowSubjects[i]))
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,6,GetHandleId(udg_PathFollowSubjects[i]))
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,7,GetHandleId(udg_PathFollowTargets[i]))
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,8,GetHandleId(udg_PathFollowEnemies[i]))
 call SaveInteger(udg_PathFollowTable,udg_PathFollowRow,9,udg_PathFollowDamageSource[i])
 call Preload("PATHMETA follow_lifetime row="+I2S(udg_PathFollowRow)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label)
 set udg_PathFollowRow=udg_PathFollowRow+1
endfunction
function PathFollowNested takes nothing returns nothing
 local integer i=udg_PathFollowNestedIndex
 call PathFollowRecord(i,"nested_before")
 call IssuePointOrder(udg_PathFollowSubjects[i],"move",1728.0,256.0+I2R(i)*288.0)
 call RemoveUnit(udg_PathFollowEnemies[i])
 call PathFollowRecord(i,"nested_after")
endfunction
function PathFollowDamage takes nothing returns nothing
 local integer i=0
 loop
  exitwhen i==7
  if GetTriggerUnit()==udg_PathFollowEnemies[i] or (i==4 and GetTriggerUnit()==udg_PathFollowSubjects[i]) then
   set udg_PathFollowDamageSource[i]=GetHandleId(GetEventDamageSource())
  endif
  if ((i==4 and GetTriggerUnit()==udg_PathFollowSubjects[i]) or (i==5 and GetTriggerUnit()==udg_PathFollowEnemies[i] and GetEventDamageSource()==udg_PathFollowSubjects[i])) and not udg_PathFollowCallback[i] then
   set udg_PathFollowCallback[i]=true
   call PathFollowRecord(i,"callback_before")
   call RemoveUnit(udg_PathFollowTargets[i])
   call PathFollowRecord(i,"callback_removed")
   set udg_PathFollowNestedIndex=i
   call TriggerExecute(udg_PathFollowNested)
   call PathFollowRecord(i,"callback_after")
  endif
  set i=i+1
 endloop
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==7
  if udg_PathProbeTick==1 then
   if ModuloInteger(i,2)==0 and i!=6 then
    call IssueTargetOrder(udg_PathFollowSubjects[i],"move",udg_PathFollowTargets[i])
   else
    call IssueTargetOrder(udg_PathFollowSubjects[i],"smart",udg_PathFollowTargets[i])
   endif
   call PathFollowRecord(i,"follow")
  elseif udg_PathProbeTick==10 then
   set udg_PathFollowEnemies[i]=CreateUnit(Player(1),'hfoo',GetUnitX(udg_PathFollowSubjects[i])+48.0,256.0+I2R(i)*288.0,180.0)
   call PauseUnit(udg_PathFollowEnemies[i],true)
   call TriggerRegisterUnitEvent(udg_PathFollowDamageTrigger,udg_PathFollowEnemies[i],EVENT_UNIT_DAMAGED)
  elseif udg_PathProbeTick==12 and ModuloInteger(i,2)==0 and i!=6 then
   call PathFollowRecord(i,"retaliation_before")
   call UnitDamageTarget(udg_PathFollowEnemies[i],udg_PathFollowSubjects[i],1.0,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
   call PathFollowRecord(i,"retaliation_after")
  elseif udg_PathProbeTick==40 and i<4 then
   call PathFollowRecord(i,"loss_before")
   if i<2 then
    call RemoveUnit(udg_PathFollowTargets[i])
   else
    call KillUnit(udg_PathFollowTargets[i])
   endif
   call PathFollowRecord(i,"loss_after")
  elseif udg_PathProbeTick==50 and i<4 then
   call RemoveUnit(udg_PathFollowTargets[i])
   set udg_PathFollowTargets[i]=CreateUnit(Player(0),'hfoo',1008.0,256.0+I2R(i)*288.0,0.0)
   call PathFollowRecord(i,"replacement_target")
  elseif udg_PathProbeTick==60 and (i<4 or i==6) then
   call KillUnit(udg_PathFollowEnemies[i])
   call PathFollowRecord(i,"enemy_death")
  elseif udg_PathProbeTick==80 and i<4 then
   call RemoveUnit(udg_PathFollowSubjects[i])
   call PathFollowRecord(i,"subject_removed")
  elseif udg_PathProbeTick==81 and i<4 then
   set udg_PathFollowSubjects[i]=CreateUnit(Player(0),'hfoo',272.0,256.0+I2R(i)*288.0,0.0)
   call PathFollowRecord(i,"replacement_subject")
  endif
  call PathFollowRecord(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=follow_lifetime")
 if udg_PathProbeTick==200 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call Preload("PATHMETA case=metadata_follow_lifetime")
 set udg_PathFollowTable=InitHashtable()
 set udg_PathFollowNested=CreateTrigger()
 set udg_PathFollowDamageTrigger=CreateTrigger()
 call TriggerAddAction(udg_PathFollowNested,function PathFollowNested)
 call TriggerAddAction(udg_PathFollowDamageTrigger,function PathFollowDamage)
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 loop
  exitwhen i==7
  set udg_PathFollowSubjects[i]=CreateUnit(Player(0),'hfoo',272.0,256.0+I2R(i)*288.0,0.0)
  set udg_PathFollowTargets[i]=CreateUnit(Player(0),'hfoo',1008.0,256.0+I2R(i)*288.0,0.0)
  call SetUnitAcquireRange(udg_PathFollowSubjects[i],128.0)
  call SetUnitAcquireRange(udg_PathFollowTargets[i],0.0)
  call PauseUnit(udg_PathFollowTargets[i],true)
  if i==4 then
   call TriggerRegisterUnitEvent(udg_PathFollowDamageTrigger,udg_PathFollowSubjects[i],EVENT_UNIT_DAMAGED)
  endif
  call PathFollowRecord(i,"initial")
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1008.0)
 call Preload("PATHTRACE tick=0 label=start_follow_lifetime")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
