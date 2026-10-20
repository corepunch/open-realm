// ORDER-01.12 b: orders while inside a mine, Peasant lumber, Orc Peon gold.
// Public producers only. Scene length is 240 ticks (gold) or 640 ticks (lumber).
globals
 unit udg_I012Worker=null
 unit udg_I012Mine=null
 unit udg_I012Hall=null
 destructable udg_I012Tree=null
 timer udg_I012Timer=null
 integer udg_I012Tick=0
 integer udg_I012Local=0
 integer udg_I012Scene=0
 integer udg_I012Enter=-1
 integer udg_I012Exit=-1
 integer udg_I012Deposit=-1
 integer udg_I012Gold=0
 integer udg_I012Lumber=0
 integer udg_I012Resource=0
 boolean udg_I012Hidden=false
endglobals
function I012B takes boolean b returns string
 if b then
  return "1"
 endif
 return "0"
endfunction
function I012Length takes integer s returns integer
 if s>=7 and s<=9 then
  return 640
 endif
 return 240
endfunction
function I012Mark takes string label returns nothing
 call Preload("I012 tick="+I2S(udg_I012Tick)+" scene="+I2S(udg_I012Scene)+" local="+I2S(udg_I012Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_I012Worker))+" hidden="+I012B(IsUnitHidden(udg_I012Worker))+" life="+R2S(GetWidgetLife(udg_I012Worker))+" gold="+I2S(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD))+" lumber="+I2S(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER))+" mine="+I2S(GetResourceAmount(udg_I012Mine))+" tree="+R2S(GetWidgetLife(udg_I012Tree))+" x="+R2S(GetUnitX(udg_I012Worker))+" y="+R2S(GetUnitY(udg_I012Worker)))
endfunction
function I012Result takes string label, boolean ok returns nothing
 call I012Mark(label+"-"+I012B(ok))
endfunction
function I012Issued takes nothing returns nothing
 if GetTriggerUnit()!=udg_I012Worker then
  return
 endif
 if GetTriggerEventId()==EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER then
  if GetOrderTargetDestructable()!=null then
   call I012Mark("issued-tree-"+I2S(GetIssuedOrderId())+"-"+I2S(GetDestructableTypeId(GetOrderTargetDestructable())))
  else
   call I012Mark("issued-target-"+I2S(GetIssuedOrderId())+"-"+I2S(GetUnitTypeId(GetOrderTargetUnit())))
  endif
 elseif GetTriggerEventId()==EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER then
  call I012Mark("issued-point-"+I2S(GetIssuedOrderId())+"-"+I2S(R2I(GetOrderPointX()))+"-"+I2S(R2I(GetOrderPointY())))
 else
  call I012Mark("issued-immediate-"+I2S(GetIssuedOrderId()))
 endif
endfunction
function I012Begin takes nothing returns nothing
 local integer s=udg_I012Scene
 if s>=7 and s<=9 then
  call I012Result("smart-tree",IssueTargetDestructableOrder(udg_I012Worker,"smart",udg_I012Tree))
 else
  call I012Result("smart-mine",IssueTargetOrder(udg_I012Worker,"smart",udg_I012Mine))
 endif
endfunction
function I012Inside takes nothing returns nothing
 local integer s=udg_I012Scene
 if s==0 then
  call I012Result("inside-smart-hall",IssueTargetOrder(udg_I012Worker,"smart",udg_I012Hall))
 elseif s==1 then
  call I012Result("inside-harvest-mine",IssueTargetOrder(udg_I012Worker,"harvest",udg_I012Mine))
 elseif s==2 then
  call I012Result("inside-hold",IssueImmediateOrder(udg_I012Worker,"holdposition"))
 elseif s==3 then
  call I012Result("inside-return",IssueImmediateOrder(udg_I012Worker,"returnresources"))
 elseif s==4 then
  call I012Result("inside-resume",IssueImmediateOrder(udg_I012Worker,"resumeharvesting"))
 elseif s==5 then
  call I012Result("inside-move",IssuePointOrder(udg_I012Worker,"move",1024,1600))
  call I012Result("inside-stop",IssueImmediateOrder(udg_I012Worker,"stop"))
 elseif s==6 then
  call I012Result("inside-autogold",IssueImmediateOrder(udg_I012Worker,"autoharvestgold"))
 endif
endfunction
function I012Exited takes nothing returns nothing
 if udg_I012Scene==8 then
  call I012Result("carry-return",IssueImmediateOrder(udg_I012Worker,"returnresources"))
 elseif udg_I012Scene==9 then
  call I012Result("carry-stop",IssueImmediateOrder(udg_I012Worker,"stop"))
 endif
endfunction
function I012Carried takes nothing returns nothing
 if udg_I012Scene==9 then
  call I012Result("carry-resume",IssueImmediateOrder(udg_I012Worker,"resumeharvesting"))
 endif
endfunction
function I012Tick takes nothing returns nothing
 local integer t=udg_I012Local
 local integer length=I012Length(udg_I012Scene)
 local integer gold=0
 local integer lumber=0
 local integer resource=0
 local boolean hidden=false
 local boolean carried=false
 if t==0 then
  set udg_I012Enter=-1
  set udg_I012Exit=-1
  set udg_I012Deposit=-1
  set udg_I012Mine=CreateUnit(Player(15),'ngol',640,1024,270)
  call SetResourceAmount(udg_I012Mine,12500)
  set udg_I012Tree=CreateDestructable('LTlt',640,1472,270,1,0)
  if udg_I012Scene==10 then
   set udg_I012Hall=CreateUnit(Player(0),'ogre',1344,1024,270)
   set udg_I012Worker=CreateUnit(Player(0),'opeo',1024,896,180)
  else
   set udg_I012Hall=CreateUnit(Player(0),'htow',1344,1024,270)
   set udg_I012Worker=CreateUnit(Player(0),'hpea',1024,896,180)
  endif
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,1000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,1000)
  set udg_I012Gold=1000
  set udg_I012Lumber=1000
  set udg_I012Resource=GetResourceAmount(udg_I012Mine)
  set udg_I012Hidden=false
  call I012Mark("create")
 elseif t==2 then
  call I012Begin()
 endif
 if t>0 and t<length-10 then
  set hidden=IsUnitHidden(udg_I012Worker)
  set gold=GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD)
  set lumber=GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER)
  set resource=GetResourceAmount(udg_I012Mine)
  if hidden and not udg_I012Hidden and udg_I012Enter<0 then
   set udg_I012Enter=t
   call I012Mark("enter")
  endif
  if udg_I012Enter>=0 and udg_I012Exit<0 and ((udg_I012Hidden and not hidden) or resource<udg_I012Resource) then
   set udg_I012Exit=t
   call I012Mark("exit")
  endif
  if udg_I012Scene>=7 and udg_I012Scene<=9 and udg_I012Exit<0 and GetUnitCurrentOrder(udg_I012Worker)==852017 then
   set udg_I012Exit=t
   call I012Mark("lumber-full")
  endif
  if (gold>udg_I012Gold or lumber>udg_I012Lumber) and udg_I012Deposit<0 then
   set udg_I012Deposit=t
   call I012Mark("deposit")
  endif
  set udg_I012Hidden=hidden
  set udg_I012Gold=gold
  set udg_I012Lumber=lumber
  set udg_I012Resource=resource
  if udg_I012Enter>=0 and t==udg_I012Enter+3 then
   call I012Inside()
  endif
  if udg_I012Exit>=0 and t==udg_I012Exit+3 then
   call I012Exited()
  endif
  if udg_I012Exit>=0 and t==udg_I012Exit+30 then
   call I012Carried()
  endif
  call I012Mark("sample")
 endif
 if t==length-8 then
  call RemoveUnit(udg_I012Worker)
  call RemoveUnit(udg_I012Hall)
  call RemoveUnit(udg_I012Mine)
  call RemoveDestructable(udg_I012Tree)
 elseif t==length-1 then
  if udg_I012Scene==10 then
   call I012Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_I012Timer)
  endif
  set udg_I012Scene=udg_I012Scene+1
  set udg_I012Local=-1
 endif
 set udg_I012Local=udg_I012Local+1
 set udg_I012Tick=udg_I012Tick+1
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
 call TriggerAddAction(issued,function I012Issued)
 set udg_I012Timer=CreateTimer()
 call TimerStart(udg_I012Timer,0.05,true,function I012Tick)
 call Preload("I012 tick=0 scene=0 label=start")
endfunction
