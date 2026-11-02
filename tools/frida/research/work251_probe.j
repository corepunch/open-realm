globals
 unit mover251=null
 unit target251=null
 timer timer251=null
 integer tick251=0
 integer scene251=0
 integer events251=0
endglobals
function Mark251 takes string label returns nothing
 call Preload("P251 tick="+I2S(tick251)+" scene="+I2S(scene251)+" label="+label+" order="+I2S(GetUnitCurrentOrder(mover251))+" events="+I2S(events251)+" x="+R2S(GetUnitX(mover251))+" y="+R2S(GetUnitY(mover251)))
endfunction
function Issued251 takes nothing returns boolean
 set events251=events251+1
 call Mark251("issued_"+I2S(GetHandleId(GetTriggerEventId())))
 return true
endfunction
function Tick251 takes nothing returns nothing
 local integer t=ModuloInteger(tick251,12)
 local boolean accepted=false
 local string command="move"
 if t==0 then
  set mover251=CreateUnit(GetLocalPlayer(),'hfoo',512.0,512.0,0.0)
  set target251=CreateUnit(GetLocalPlayer(),'hfoo',1536.0,1024.0,0.0)
  call SetUnitAcquireRange(mover251,0.0)
  call SetUnitAcquireRange(target251,0.0)
  if scene251<2 then
   call RemoveUnit(target251)
   set target251=mover251
  elseif scene251<4 then
   call ShowUnit(target251,false)
  elseif scene251<6 then
   call KillUnit(target251)
  endif
  set events251=0
  call IssuePointOrder(mover251,"move",2000.0,1500.0)
  call Mark251("before")
 elseif t==2 then
  if ModuloInteger(scene251,2)==1 then
   set command="smart"
  endif
  set accepted=IssueTargetOrder(mover251,command,target251)
  if accepted then
   call Mark251("accepted")
  else
   call Mark251("rejected")
  endif
 elseif t==8 then
  call Mark251("later")
 elseif t==10 then
  call IssueImmediateOrder(mover251,"stop")
  if target251!=mover251 then
   call RemoveUnit(target251)
  endif
  call RemoveUnit(mover251)
 elseif t==11 then
  set scene251=scene251+1
  if scene251==8 then
   call Mark251("complete")
   call PreloadGenEnd("rs-work251.txt")
   call PauseTimer(timer251)
  endif
 endif
 set tick251=tick251+1
endfunction
function main takes nothing returns nothing
 local trigger listener=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call TriggerRegisterPlayerUnitEvent(listener,GetLocalPlayer(),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(listener,GetLocalPlayer(),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddCondition(listener,Condition(function Issued251))
 call Mark251("start")
 set timer251=CreateTimer()
 call TimerStart(timer251,0.1,true,function Tick251)
endfunction
