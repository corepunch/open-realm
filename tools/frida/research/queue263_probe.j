globals
 unit actor263=null
 unit target263=null
 timer timer263=null
 integer tick263=0
 string variant263="@VARIANT@"
endglobals
function Event263 takes nothing returns boolean
 call Preload("Q263 tick="+I2S(tick263)+" label=issued event="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(GetTriggerUnit()))+" target="+I2S(GetUnitUserData(GetOrderTargetUnit()))+" x="+R2S(GetOrderPointX())+" y="+R2S(GetOrderPointY()))
 return true
endfunction
function Tick263 takes nothing returns nothing
 set tick263=tick263+1
 if tick263==1 then
  call PreloadGenEnd("queue263-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if tick263<=30 then
  call SetCameraPosition(1024.0,512.0)
 endif
 if tick263==2 then
  call IssuePointOrder(actor263,"move",512.0,1800.0)
 endif
 if tick263==80 then
  if variant263=="removed" or variant263=="repair" then
   call RemoveUnit(target263)
  endif
  if variant263=="invulnerable" then
   call SetUnitInvulnerable(target263,true)
  endif
  if variant263=="cancel" then
   call IssueImmediateOrder(actor263,"stop")
  endif
  call Preload("Q263 tick=80 label=mutation variant="+variant263)
 endif
 if tick263==260 then
  call IssueImmediateOrder(actor263,"stop")
  call Preload("Q263 tick=260 label=cleanup variant="+variant263)
 endif
 if tick263/10*10==tick263 then
  call Preload("Q263 tick="+I2S(tick263)+" label=sample head="+I2S(GetUnitCurrentOrder(actor263))+" x="+R2S(GetUnitX(actor263))+" y="+R2S(GetUnitY(actor263)))
 endif
 if tick263==300 then
  call Preload("Q263 tick=300 label=complete variant="+variant263)
  call PreloadGenEnd("queue263-"+variant263+".txt")
  call PauseTimer(timer263)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger listener=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(3),PLAYER_STATE_RESOURCE_GOLD,10000)
 call SetPlayerState(Player(3),PLAYER_STATE_RESOURCE_LUMBER,10000)
 call FogEnable(false)
 call FogMaskEnable(false)
 if variant263=="repair" then
  set actor263=CreateUnit(Player(3),'hpea',512.0,512.0,0.0)
  set target263=CreateUnit(Player(3),'hbar',1024.0,512.0,0.0)
  call SetWidgetLife(target263,100.0)
 else
  set actor263=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
  set target263=CreateUnit(Player(1),'hfoo',1024.0,512.0,0.0)
 endif
 call SetUnitUserData(actor263,1)
 call SetUnitUserData(target263,2)
 call SetUnitMoveSpeed(actor263,150.0)
 call SetUnitInvulnerable(actor263,true)
 call PauseUnit(target263,true)
 call SetUnitScale(target263,3.0,3.0,3.0)
 call TriggerRegisterPlayerUnitEvent(listener,Player(3),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(listener,Player(3),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(listener,Player(3),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddCondition(listener,Condition(function Event263))
 call ClearSelection()
 call SelectUnit(actor263,true)
 call SetCameraBounds(-2048.0,-2048.0,-2048.0,4096.0,4096.0,4096.0,4096.0,-2048.0)
 call ResetToGameCamera(0.0)
 call SetCameraPosition(1024.0,512.0)
 call Preload("Q263 tick=0 label=start variant="+variant263+" player="+I2S(GetPlayerId(GetLocalPlayer())))
 set timer263=CreateTimer()
 call TimerStart(timer263,0.1,true,function Tick263)
endfunction
