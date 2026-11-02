globals
 unit array udg_O118Subjects
 unit array udg_O118Enemies
 integer array udg_O118DamageSource
 integer array udg_O118Accepted
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_O118Row=0
 hashtable udg_O118Table=null
 trigger udg_O118DamageTrigger=null
endglobals
// ORDER-01.18 Patrol composition probe (public JASS plus genuine owned Shift input from the capture script).
// 0 y=300  : Patrol (1500,300) through paused enemy (900,300); KillUnit at tick 80 (combat composition internals).
// 1 y=520  : Patrol to a point 60 from the unit (below the 100-unit expansion threshold).
// 2 y=600  : Patrol to a point exactly 100 from the unit (threshold boundary).
// 3 y=680  : Patrol to a point 99.99 from the unit.
// 4 y=1280 : speed100 Patrol (1500,1280); selected at tick 18, Shift+M click at tick 20 queues Move behind the active leg.
// 5 y=960  : speed100 Move (600,960); selected at tick 2, Shift+P click tick 3 and Shift+M click tick 6.
function O118Y takes integer i returns real
 if i==0 then
  return 300.0
 elseif i==1 then
  return 520.0
 elseif i==2 then
  return 600.0
 elseif i==3 then
  return 680.0
 elseif i==4 then
  return 1280.0
 endif
 return 960.0
endfunction
function O118Store takes integer i, string label, integer order, real word3 returns nothing
 call SaveInteger(udg_O118Table,udg_O118Row,0,udg_PathProbeTick)
 call SaveInteger(udg_O118Table,udg_O118Row,1,i)
 call SaveInteger(udg_O118Table,udg_O118Row,2,order)
 call SaveReal(udg_O118Table,udg_O118Row,3,word3)
 call SaveReal(udg_O118Table,udg_O118Row,4,GetUnitX(udg_O118Subjects[i]))
 call SaveReal(udg_O118Table,udg_O118Row,5,GetUnitY(udg_O118Subjects[i]))
 call SaveInteger(udg_O118Table,udg_O118Row,6,GetHandleId(udg_O118Subjects[i]))
 call SaveInteger(udg_O118Table,udg_O118Row,7,GetHandleId(udg_O118Enemies[i]))
 call SaveInteger(udg_O118Table,udg_O118Row,8,udg_O118DamageSource[i])
 call SaveInteger(udg_O118Table,udg_O118Row,9,udg_O118Accepted[i])
 call Preload("O118 row="+I2S(udg_O118Row)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label+" order="+I2S(order)+" w3="+R2S(word3)+" x="+R2S(GetUnitX(udg_O118Subjects[i]))+" y="+R2S(GetUnitY(udg_O118Subjects[i]))+" src="+I2S(udg_O118DamageSource[i])+" acc="+I2S(udg_O118Accepted[i]))
 set udg_O118Row=udg_O118Row+1
endfunction
function O118Record takes integer i, string label returns nothing
 call O118Store(i,label,GetUnitCurrentOrder(udg_O118Subjects[i]),GetUnitState(udg_O118Enemies[i],UNIT_STATE_LIFE))
endfunction
function O118Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function O118Damage takes nothing returns nothing
 if udg_PathProbeTick>=400 then
  return
 endif
 if GetTriggerUnit()==udg_O118Enemies[0] then
  set udg_O118DamageSource[0]=GetHandleId(GetEventDamageSource())
  call O118Store(0,"damage",GetUnitCurrentOrder(GetEventDamageSource()),GetEventDamage())
 endif
endfunction
function O118Select takes integer i returns nothing
 call ClearSelection()
 call SelectUnit(udg_O118Subjects[i],true)
 call SetCameraPosition(1008.0,O118Y(i))
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==6
  if udg_PathProbeTick==1 then
   if i==0 then
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"patrol",1500.0,O118Y(i)))
   elseif i==1 then
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"patrol",332.0,O118Y(i)))
   elseif i==2 then
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"patrol",372.0,O118Y(i)))
   elseif i==3 then
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"patrol",371.99,O118Y(i)))
   elseif i==4 then
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"patrol",1500.0,O118Y(i)))
   else
    set udg_O118Accepted[i]=O118Bool(IssuePointOrder(udg_O118Subjects[i],"move",600.0,O118Y(i)))
   endif
   call O118Record(i,"issue")
  elseif udg_PathProbeTick==2 and i==5 then
   call O118Select(i)
   call O118Record(i,"select")
  elseif udg_PathProbeTick==18 and i==4 then
   call O118Select(i)
   call O118Record(i,"select")
  elseif udg_PathProbeTick==80 and i==0 then
   call O118Record(i,"kill_before")
   call KillUnit(udg_O118Enemies[i])
   call O118Record(i,"kill_after")
  endif
  call O118Record(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=o118")
 if udg_PathProbeTick==400 then
  call Preload("O118 complete rows="+I2S(udg_O118Row))
  call Preload("PATHMETA complete")
  call PreloadGenEnd("rs-o118-@VARIANT@.txt")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("PATHMETA case=metadata_o118_patrol")
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetCameraBounds(-1024.0,-1024.0,3072.0,3072.0,-1024.0,3072.0,3072.0,-1024.0)
 set udg_O118Table=InitHashtable()
 set udg_O118DamageTrigger=CreateTrigger()
 call TriggerAddAction(udg_O118DamageTrigger,function O118Damage)
 loop
  exitwhen i==6
  set udg_O118Subjects[i]=CreateUnit(GetLocalPlayer(),'hfoo',272.0,O118Y(i),0.0)
  set udg_O118Accepted[i]=-1
  if i==0 then
   call SetUnitAcquireRange(udg_O118Subjects[i],128.0)
   set udg_O118Enemies[i]=CreateUnit(Player(1),'hfoo',900.0,O118Y(i),180.0)
   call SetUnitState(udg_O118Enemies[i],UNIT_STATE_MAX_LIFE,10000.0)
   call SetUnitState(udg_O118Enemies[i],UNIT_STATE_LIFE,10000.0)
   call PauseUnit(udg_O118Enemies[i],true)
   call TriggerRegisterUnitEvent(udg_O118DamageTrigger,udg_O118Enemies[i],EVENT_UNIT_DAMAGED)
  else
   call SetUnitAcquireRange(udg_O118Subjects[i],0.0)
  endif
  if i>=4 then
   call SetUnitMoveSpeed(udg_O118Subjects[i],100.0)
  endif
  call O118Record(i,"initial")
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("PATHTRACE tick=0 label=start_o118")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
