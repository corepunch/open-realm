globals
 unit array udg_O110Subjects
 unit array udg_O110Enemies
 unit udg_O110Invulnerable=null
 integer array udg_O110DamageSource
 integer array udg_O110Accepted
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_O110Row=0
 hashtable udg_O110Table=null
 trigger udg_O110DamageTrigger=null
endglobals
// ORDER-01.10 public Attack/Attack Move/Attack Ground lifetime probe (research, public JASS only).
// Variants a={0,1,3} b={2,4,9} c={5,6,7} d={8,10,11,12}; rows y=300/960/1620 (660 apart, no cross-row acquisition). Records: tick, case, current order, enemy life or damage, x, y, subject/enemy handles,
// last damage source, last public issue result. Preload text duplicates every record for observer-free controls.
function O110Active takes integer i returns boolean
 if "@VARIANT@"=="a" then
  return i==0 or i==1 or i==3
 elseif "@VARIANT@"=="b" then
  return i==2 or i==4 or i==9
 elseif "@VARIANT@"=="c" then
  return i==5 or i==6 or i==7
 endif
 return i==8 or i==10 or i==11 or i==12
endfunction
function O110Y takes integer i returns real
 if i==0 or i==2 or i==5 or i==8 then
  return 300.0
 elseif i==1 or i==4 or i==6 or i==10 then
  return 960.0
 elseif i==12 then
  return 1290.0
 endif
 return 1620.0
endfunction
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
 local integer i=0
 if udg_PathProbeTick>=200 then
  return
 endif
 loop
  exitwhen i==12
  if GetTriggerUnit()==udg_O110Enemies[i] then
   set udg_O110DamageSource[i]=GetHandleId(GetEventDamageSource())
   call O110Store(i,"damage",GetUnitCurrentOrder(GetEventDamageSource()),GetEventDamage())
  endif
  set i=i+1
 endloop
endfunction
function O110Enemy takes integer i, integer kind, real x returns nothing
 set udg_O110Enemies[i]=CreateUnit(Player(1),kind,x,O110Y(i),180.0)
 call SetUnitState(udg_O110Enemies[i],UNIT_STATE_MAX_LIFE,10000.0)
 call SetUnitState(udg_O110Enemies[i],UNIT_STATE_LIFE,10000.0)
 call PauseUnit(udg_O110Enemies[i],true)
 call TriggerRegisterUnitEvent(udg_O110DamageTrigger,udg_O110Enemies[i],EVENT_UNIT_DAMAGED)
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==13
  if not O110Active(i) then
  elseif udg_PathProbeTick==1 and i==12 then
   set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Subjects[i]))
   call O110Record(i,"attack_self")
  elseif udg_PathProbeTick==1 then
   if i==0 or i==4 or i==5 or i==9 or i==11 then
    set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Enemies[i]))
   elseif i==1 or i==7 then
    set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"attack",1728.0,O110Y(i)))
   elseif i==2 then
    set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"attackground",1700.0,O110Y(i)))
   elseif i==10 then
    set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"attackground",1500.0,O110Y(i)))
   elseif i==3 then
    set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Enemies[i]))
   elseif i==8 then
    set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attackonce",udg_O110Enemies[i]))
   endif
   call O110Record(i,"issue")
  elseif udg_PathProbeTick==10 and i==6 then
   call O110Enemy(i,'hfoo',352.0)
   call O110Record(i,"enemy_created")
  elseif udg_PathProbeTick==12 and i==4 then
   set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Invulnerable))
   call O110Record(i,"reject_attack_invulnerable")
  elseif (udg_PathProbeTick==1 or udg_PathProbeTick==22) and i==12 then
   set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Subjects[i]))
   call O110Record(i,"attack_self")
  elseif udg_PathProbeTick==20 and i==12 then
   set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"move",900.0,O110Y(i)))
   call O110Record(i,"move")
  elseif udg_PathProbeTick==25 and i==4 then
   set udg_O110Accepted[i]=O110Bool(IssuePointOrder(udg_O110Subjects[i],"move",1500.0,O110Y(i)))
   call O110Record(i,"replace_move")
  elseif udg_PathProbeTick==40 and i==5 then
   call O110Record(i,"death_before")
   call KillUnit(udg_O110Subjects[i])
   call O110Record(i,"death_after")
  elseif udg_PathProbeTick==40 and i==11 then
   call IssueImmediateOrder(udg_O110Subjects[i],"stop")
   call O110Record(i,"stop")
  elseif udg_PathProbeTick==42 and i==5 then
   set udg_O110Subjects[i]=CreateUnit(Player(0),'hfoo',272.0,O110Y(i),0.0)
   call SetUnitAcquireRange(udg_O110Subjects[i],128.0)
   call O110Record(i,"replacement")
  elseif udg_PathProbeTick==45 and i==9 then
   call O110Record(i,"remove_before")
   call RemoveUnit(udg_O110Enemies[i])
   call O110Record(i,"remove_after")
  elseif udg_PathProbeTick==45 and i==4 then
   set udg_O110Accepted[i]=O110Bool(IssueTargetOrder(udg_O110Subjects[i],"attack",udg_O110Enemies[i]))
   call O110Record(i,"reissue_attack")
  elseif udg_PathProbeTick==60 and (i==0 or i==1 or i==3 or i==6) then
   call O110Record(i,"kill_before")
   call KillUnit(udg_O110Enemies[i])
   call O110Record(i,"kill_after")
  elseif udg_PathProbeTick==90 and i==4 then
   call O110Record(i,"kill_before")
   call KillUnit(udg_O110Enemies[i])
   call O110Record(i,"kill_after")
  elseif udg_PathProbeTick==150 and (i==2 or i==10) then
   call IssueImmediateOrder(udg_O110Subjects[i],"stop")
   call O110Record(i,"stop")
  endif
  if O110Active(i) then
   call O110Record(i,"sample")
  endif
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=o110")
 if udg_PathProbeTick==200 then
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
 call Preload("PATHMETA case=metadata_o110_attack")
 set udg_O110Table=InitHashtable()
 set udg_O110DamageTrigger=CreateTrigger()
 call TriggerAddAction(udg_O110DamageTrigger,function O110Damage)
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 loop
  exitwhen i==13
  if O110Active(i) then
   if i==2 then
    set udg_O110Subjects[i]=CreateUnit(Player(0),'hmtm',272.0,O110Y(i),0.0)
   else
    set udg_O110Subjects[i]=CreateUnit(Player(0),'hfoo',272.0,O110Y(i),0.0)
   endif
   call SetUnitAcquireRange(udg_O110Subjects[i],128.0)
   if i==2 then
    call O110Enemy(i,'hfoo',1700.0)
   elseif i==3 then
    call O110Enemy(i,'hgry',1008.0)
   elseif i==0 or i==1 or i==4 or i==5 or i==8 or i==9 or i==11 then
    call O110Enemy(i,'hfoo',1008.0)
   endif
   set udg_O110Accepted[i]=-1
   call O110Record(i,"initial")
  endif
  set i=i+1
 endloop
 if O110Active(4) then
  set udg_O110Invulnerable=CreateUnit(Player(1),'hfoo',1500.0,1290.0,180.0)
  call PauseUnit(udg_O110Invulnerable,true)
  call SetUnitInvulnerable(udg_O110Invulnerable,true)
 endif
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1008.0)
 call Preload("PATHTRACE tick=0 label=start_o110")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
