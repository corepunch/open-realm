// ORDER-01.12 Human Peasant gold Harvest/Return current-order ownership.
// Public producers only: Smart/Harvest/Return/Stop/Move natives, Kill/Remove.
globals
 unit udg_H012Worker=null
 unit udg_H012Peer=null
 unit udg_H012Mine=null
 unit udg_H012Hall=null
 timer udg_H012Timer=null
 integer udg_H012Tick=0
 integer udg_H012Scene=0
 integer udg_H012Enter=-1
 integer udg_H012Exit=-1
 integer udg_H012Deposit=-1
 integer udg_H012Gold=0
 integer udg_H012Resource=0
 boolean udg_H012Hidden=false
endglobals
function H012B takes boolean b returns string
 if b then
  return "1"
 endif
 return "0"
endfunction
function H012Mark takes string label returns nothing
 call Preload("H012 tick="+I2S(udg_H012Tick)+" scene="+I2S(udg_H012Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_H012Worker))+" peer="+I2S(GetUnitCurrentOrder(udg_H012Peer))+" hidden="+H012B(IsUnitHidden(udg_H012Worker))+" peerhidden="+H012B(IsUnitHidden(udg_H012Peer))+" life="+R2S(GetWidgetLife(udg_H012Worker))+" gold="+I2S(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD))+" mine="+I2S(GetResourceAmount(udg_H012Mine))+" minelife="+R2S(GetWidgetLife(udg_H012Mine))+" hall="+R2S(GetWidgetLife(udg_H012Hall))+" x="+R2S(GetUnitX(udg_H012Worker))+" y="+R2S(GetUnitY(udg_H012Worker))+" px="+R2S(GetUnitX(udg_H012Peer))+" py="+R2S(GetUnitY(udg_H012Peer)))
endfunction
function H012Result takes string label, boolean ok returns nothing
 call H012Mark(label+"-"+H012B(ok))
endfunction
function H012Issued takes nothing returns nothing
 local unit u=GetTriggerUnit()
 local string who="other"
 if u==udg_H012Worker then
  set who="worker"
 elseif u==udg_H012Peer then
  set who="peer"
 endif
 if GetTriggerEventId()==EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER then
  call H012Mark("issued-target-"+who+"-"+I2S(GetIssuedOrderId())+"-"+I2S(GetUnitTypeId(GetOrderTargetUnit())))
 elseif GetTriggerEventId()==EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER then
  call H012Mark("issued-point-"+who+"-"+I2S(GetIssuedOrderId())+"-"+I2S(R2I(GetOrderPointX()))+"-"+I2S(R2I(GetOrderPointY())))
 else
  call H012Mark("issued-immediate-"+who+"-"+I2S(GetIssuedOrderId()))
 endif
 set u=null
endfunction
function H012Begin takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==0 or s==2 or s==3 or s==4 or s==5 or s==6 or s==7 or s==8 or s==9 or s==10 or s==11 or s==12 or s==13 or s==14 or s==15 or s==16 or s==17 or s==20 then
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Mine)
  call H012Result("smart-mine",ok)
 elseif s==1 then
  set ok=IssueTargetOrder(udg_H012Worker,"harvest",udg_H012Mine)
  call H012Result("harvest-mine",ok)
 elseif s==18 then
  set ok=IssueImmediateOrder(udg_H012Worker,"returnresources")
  call H012Result("return-empty",ok)
  set ok=IssueTargetOrder(udg_H012Worker,"harvest",udg_H012Hall)
  call H012Result("harvest-hall",ok)
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Hall)
  call H012Result("smart-hall-empty",ok)
  set ok=IssueImmediateOrder(udg_H012Worker,"resumeharvesting")
  call H012Result("resume-empty",ok)
 elseif s==19 then
  set ok=IssueImmediateOrder(udg_H012Worker,"autoharvestgold")
  call H012Result("autoharvestgold",ok)
 endif
 if s==17 then
  set ok=IssueTargetOrder(udg_H012Peer,"smart",udg_H012Mine)
  call H012Result("peer-smart-mine",ok)
 endif
endfunction
function H012Early takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==2 then
  set ok=IssueImmediateOrder(udg_H012Worker,"stop")
  call H012Result("stop-approach",ok)
 elseif s==3 then
  set ok=IssuePointOrder(udg_H012Worker,"move",1024,1600)
  call H012Result("move-approach",ok)
 elseif s==14 then
  call KillUnit(udg_H012Mine)
  call H012Mark("kill-mine-approach")
 elseif s==18 then
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Mine)
  call H012Result("smart-mine-late",ok)
 endif
endfunction
function H012Late takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==2 then
  set ok=IssueImmediateOrder(udg_H012Worker,"resumeharvesting")
  call H012Result("resume-after-stop",ok)
 elseif s==3 then
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Mine)
  call H012Result("smart-after-move",ok)
 endif
endfunction
function H012Inside takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==4 then
  set ok=IssuePointOrder(udg_H012Worker,"move",1024,1600)
  call H012Result("inside-move",ok)
 elseif s==5 then
  set ok=IssueImmediateOrder(udg_H012Worker,"stop")
  call H012Result("inside-stop",ok)
 elseif s==15 then
  call RemoveUnit(udg_H012Mine)
  call H012Mark("inside-remove-mine")
 elseif s==20 then
  call KillUnit(udg_H012Worker)
  call H012Mark("inside-kill-worker")
 endif
endfunction
function H012Exited takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==6 then
  set ok=IssuePointOrder(udg_H012Worker,"move",1024,1600)
  call H012Result("carry-move",ok)
 elseif s==7 then
  set ok=IssueImmediateOrder(udg_H012Worker,"stop")
  call H012Result("carry-stop",ok)
 elseif s==8 then
  set ok=IssueImmediateOrder(udg_H012Worker,"returnresources")
  call H012Result("carry-return",ok)
 elseif s==9 then
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Hall)
  call H012Result("carry-smart-hall",ok)
 elseif s==10 then
  set ok=IssueTargetOrder(udg_H012Worker,"smart",udg_H012Mine)
  call H012Result("carry-smart-mine",ok)
 elseif s==11 then
  set ok=IssueTargetOrder(udg_H012Worker,"harvest",udg_H012Mine)
  call H012Result("carry-harvest-mine",ok)
 elseif s==12 then
  call KillUnit(udg_H012Worker)
  call H012Mark("carry-kill-worker")
 elseif s==13 then
  call RemoveUnit(udg_H012Hall)
  call H012Mark("carry-remove-hall")
 endif
endfunction
function H012Carried takes nothing returns nothing
 local integer s=udg_H012Scene
 local boolean ok=false
 if s==6 then
  set ok=IssueImmediateOrder(udg_H012Worker,"returnresources")
  call H012Result("moved-return",ok)
 elseif s==7 then
  set ok=IssueImmediateOrder(udg_H012Worker,"resumeharvesting")
  call H012Result("stopped-resume",ok)
 elseif s==13 then
  set udg_H012Hall=CreateUnit(Player(0),'htow',1344,1024,270)
  call H012Mark("carry-new-hall")
 endif
endfunction
function H012Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_H012Tick,240)
 local integer gold=0
 local integer resource=0
 local boolean hidden=false
 if t==0 then
  set udg_H012Enter=-1
  set udg_H012Exit=-1
  set udg_H012Deposit=-1
  set udg_H012Mine=CreateUnit(Player(15),'ngol',640,1024,270)
  if udg_H012Scene==16 then
   call SetResourceAmount(udg_H012Mine,10)
  else
   call SetResourceAmount(udg_H012Mine,12500)
  endif
  set udg_H012Hall=CreateUnit(Player(0),'htow',1344,1024,270)
  set udg_H012Worker=CreateUnit(Player(0),'hpea',1024,896,180)
  if udg_H012Scene==17 then
   set udg_H012Peer=CreateUnit(Player(0),'hpea',1024,1152,180)
  else
   set udg_H012Peer=null
  endif
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,1000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,1000)
  set udg_H012Gold=1000
  set udg_H012Resource=GetResourceAmount(udg_H012Mine)
  set udg_H012Hidden=false
  call H012Mark("create")
 elseif t==2 then
  call H012Begin()
 elseif t==6 then
  call H012Early()
 elseif t==20 then
  call H012Late()
 endif
 if t>0 and t<230 then
  set hidden=IsUnitHidden(udg_H012Worker)
  set gold=GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD)
  set resource=GetResourceAmount(udg_H012Mine)
  if hidden and not udg_H012Hidden and udg_H012Enter<0 then
   set udg_H012Enter=t
   call H012Mark("enter")
  endif
  if udg_H012Enter>=0 and udg_H012Exit<0 and ((udg_H012Hidden and not hidden) or resource<udg_H012Resource) then
   set udg_H012Exit=t
   call H012Mark("exit")
  endif
  if gold>udg_H012Gold and udg_H012Deposit<0 then
   set udg_H012Deposit=t
   call H012Mark("deposit")
  endif
  set udg_H012Hidden=hidden
  set udg_H012Gold=gold
  set udg_H012Resource=resource
  if udg_H012Enter>=0 and t==udg_H012Enter+3 then
   call H012Inside()
  endif
  if udg_H012Exit>=0 and t==udg_H012Exit+3 then
   call H012Exited()
  endif
  if udg_H012Exit>=0 and t==udg_H012Exit+30 then
   call H012Carried()
  endif
  call H012Mark("sample")
 endif
 if t==232 then
  call RemoveUnit(udg_H012Worker)
  call RemoveUnit(udg_H012Peer)
  call RemoveUnit(udg_H012Hall)
  call RemoveUnit(udg_H012Mine)
 elseif t==239 then
  if udg_H012Scene==20 then
   call H012Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_H012Timer)
  endif
  set udg_H012Scene=udg_H012Scene+1
 endif
 set udg_H012Tick=udg_H012Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger issued=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1012)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call TriggerRegisterPlayerUnitEvent(issued,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(issued,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(issued,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(issued,function H012Issued)
 set udg_H012Timer=CreateTimer()
 call TimerStart(udg_H012Timer,0.05,true,function H012Tick)
 call Preload("H012 tick=0 scene=0 label=start")
endfunction
