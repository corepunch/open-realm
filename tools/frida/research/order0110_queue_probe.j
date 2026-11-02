globals
 unit array udg_O110Subjects
 unit array udg_O110Enemies
 integer array udg_O110DamageSource
 integer array udg_O110Accepted
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_O110Row=0
 hashtable udg_O110Table=null
 trigger udg_O110DamageTrigger=null
endglobals
// ORDER-01.10 queued handoff probe (public JASS plus genuine owned Shift input from the capture script).
// Case 0 (y=320): JASS Move to (1008,320); Shift+A click at tick 5 queues Attack Move behind it.
// Case 1 (y=1280): JASS Attack Move to (1008,1280) through a paused enemy at (640,1280); Shift+M click at
// tick 155 queues Move behind the active Attack Move; KillUnit(enemy) at tick 200.
function O110Store takes integer i, string label, integer order, real word3 returns nothing
 call SaveInteger(udg_O110Table,udg_O110Row,0,udg_PathProbeTick)
 call SaveInteger(udg_O110Table,udg_O110Row,1,i)
 call SaveInteger(udg_O110Table,udg_O110Row,2,order)
 call SaveReal(udg_O110Table,udg_O110Row,3,word3)
 call SaveReal(udg_O110Table,udg_O110Row,4,GetUnitX(udg_O110Subjects[i]))
 call SaveReal(udg_O110Table,udg_O110Row,5,GetUnitY(udg_O110Subjects[i]))
 call SaveInteger(udg_O110Table,udg_O110Row,6,GetHandleId(udg_O110Subjects[i]))
 call SaveInteger(udg_O110Table,udg_O110Row,7,GetHandleId(udg_O110Enemies[i]))
 call SaveInteger(udg_O110Table,udg_O110Row,8,udg_O110DamageSource[i])
 call SaveInteger(udg_O110Table,udg_O110Row,9,udg_O110Accepted[i])
 call Preload("O110 row="+I2S(udg_O110Row)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label+" order="+I2S(order)+" w3="+R2S(word3)+" x="+R2S(GetUnitX(udg_O110Subjects[i]))+" y="+R2S(GetUnitY(udg_O110Subjects[i]))+" src="+I2S(udg_O110DamageSource[i])+" acc="+I2S(udg_O110Accepted[i]))
 set udg_O110Row=udg_O110Row+1
endfunction
function O110Record takes integer i, string label returns nothing
 call O110Store(i,label,GetUnitCurrentOrder(udg_O110Subjects[i]),GetUnitState(udg_O110Enemies[i],UNIT_STATE_LIFE))
endfunction
function O110Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function O110Damage takes nothing returns nothing
 if udg_PathProbeTick>=300 then
  return
 endif
 if GetTriggerUnit()==udg_O110Enemies[1] then
  set udg_O110DamageSource[1]=GetHandleId(GetEventDamageSource())
  call O110Store(1,"damage",GetUnitCurrentOrder(GetEventDamageSource()),GetEventDamage())
 endif
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==2
  if udg_PathProbeTick==1+i*150 then
   call ClearSelection()
   call SelectUnit(udg_O110Subjects[i],true)
   call SetCameraPosition(1008.0,320.0+I2R(i)*960.0)
   if i==0 then
    set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"move",1008.0,320.0))
   else
    set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"attack",1008.0,1280.0))
   endif
   call O110Record(i,"order")
  elseif udg_PathProbeTick==200 and i==1 then
   call O110Record(i,"kill_before")
   call KillUnit(udg_O110Enemies[i])
   call O110Record(i,"kill_after")
  endif
  call O110Record(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=o110q")
 if udg_PathProbeTick==300 then
  call Preload("O110 complete rows="+I2S(udg_O110Row))
  call Preload("PATHMETA complete")
  call PreloadGenEnd("rs-o110-@VARIANT@.txt")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("PATHMETA case=metadata_o110_queue")
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetCameraBounds(-1024.0,-1024.0,3072.0,3072.0,-1024.0,3072.0,3072.0,-1024.0)
 set udg_O110Table=InitHashtable()
 set udg_O110DamageTrigger=CreateTrigger()
 call TriggerAddAction(udg_O110DamageTrigger,function O110Damage)
 loop
  exitwhen i==2
  set udg_O110Subjects[i]=CreateUnit(GetLocalPlayer(),'hfoo',272.0,320.0+I2R(i)*960.0,0.0)
  call SetUnitMoveSpeed(udg_O110Subjects[i],100.0)
  set udg_O110Accepted[i]=-1
  if i==0 then
   call SetUnitAcquireRange(udg_O110Subjects[i],0.0)
  else
   call SetUnitAcquireRange(udg_O110Subjects[i],128.0)
   set udg_O110Enemies[i]=CreateUnit(Player(1),'hfoo',640.0,1280.0,180.0)
   call SetUnitState(udg_O110Enemies[i],UNIT_STATE_MAX_LIFE,10000.0)
   call SetUnitState(udg_O110Enemies[i],UNIT_STATE_LIFE,10000.0)
   call PauseUnit(udg_O110Enemies[i],true)
   call TriggerRegisterUnitEvent(udg_O110DamageTrigger,udg_O110Enemies[i],EVENT_UNIT_DAMAGED)
  endif
  call O110Record(i,"initial")
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("PATHTRACE tick=0 label=start_o110q")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
