globals
 unit udg_P287Unit=null
 timer udg_P287Timer=null
 integer udg_P287Tick=0
 integer udg_P287Scene=0
 boolean udg_P287Record=false
 boolean udg_P287Nested=false
endglobals
function P287Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function P287Mark takes string label returns nothing
 call Preload("P287 tick="+I2S(udg_P287Tick)+" scene="+I2S(udg_P287Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_P287Unit))+" paused="+I2S(P287Bool(IsUnitPaused(udg_P287Unit)))+" x="+R2S(GetUnitX(udg_P287Unit))+" y="+R2S(GetUnitY(udg_P287Unit))+" px="+R2S(GetOrderPointX())+" py="+R2S(GetOrderPointY())+" target="+I2S(P287Bool(GetOrderTargetUnit()!=null))+" defend="+I2S(GetUnitAbilityLevel(udg_P287Unit,'Adef'))+" speed="+R2S(GetUnitMoveSpeed(udg_P287Unit)))
endfunction
function P287Nested takes integer command returns nothing
 local boolean take=false
 local boolean accepted=false
 local integer action=udg_P287Scene
 if udg_P287Nested then
  return
 endif
 // Script lookup has no name for this688d30 core selector.
 if command==851973 then
  set take=action>=5 and action<=8 or action==11 or action==13
 elseif command==OrderId("undefend") then
  set take=action==9 or action==10 or action==12 or action==14
 elseif command==OrderId("move") and ModuloInteger(udg_P287Tick,10)>=4 then
  set take=action==15 or action==16
 endif
 if not take then
  return
 endif
 set udg_P287Nested=true
 if action==5 or action==9 or action==16 then
  set accepted=IssuePointOrder(udg_P287Unit,"move",1040,1040)
  if accepted then
   call P287Mark("nested-move-true")
  else
   call P287Mark("nested-move-false")
  endif
 elseif action==6 or action==10 or action==15 then
  set accepted=IssueImmediateOrder(udg_P287Unit,"stop")
  if accepted then
   call P287Mark("nested-stop-true")
  else
   call P287Mark("nested-stop-false")
  endif
 elseif action==7 or action==14 then
  call PauseUnit(udg_P287Unit,false)
  call P287Mark("nested-unpause")
 elseif action==8 then
  call RemoveUnit(udg_P287Unit)
  call P287Mark("nested-remove")
 elseif action==11 or action==12 then
  set accepted=UnitRemoveAbility(udg_P287Unit,'Adef')
  call P287Mark("nested-remove-ability")
 elseif action==13 then
  call PauseUnit(udg_P287Unit,true)
  call P287Mark("nested-pause")
 endif
endfunction
function P287PlayerImmediate takes nothing returns nothing
 if udg_P287Record and GetTriggerUnit()==udg_P287Unit then
  call P287Mark("player-immediate-"+I2S(GetIssuedOrderId()))
  call P287Nested(GetIssuedOrderId())
 endif
endfunction
function P287PlayerPoint takes nothing returns nothing
 if udg_P287Record and GetTriggerUnit()==udg_P287Unit then
  call P287Mark("player-point-"+I2S(GetIssuedOrderId()))
  call P287Nested(GetIssuedOrderId())
 endif
endfunction
function P287UnitImmediate takes nothing returns nothing
 if udg_P287Record then
  call P287Mark("unit-immediate-"+I2S(GetIssuedOrderId()))
 endif
endfunction
function P287UnitPoint takes nothing returns nothing
 if udg_P287Record then
  call P287Mark("unit-point-"+I2S(GetIssuedOrderId()))
 endif
endfunction
function P287Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_P287Tick,10)
 local integer action=udg_P287Scene
 local integer kind='hfoo'
 local trigger t=null
 if phase==0 then
  set udg_P287Record=false
  set udg_P287Nested=false
  if action==17 or action==19 then
   set kind='hrif'
  elseif action==18 then
   set kind='htow'
  endif
  set udg_P287Unit=CreateUnit(Player(0),kind,656,656,0)
  call SetUnitAcquireRange(udg_P287Unit,0)
  if action==19 then
   call UnitAddAbility(udg_P287Unit,'Adef')
  endif
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_P287Unit,EVENT_UNIT_ISSUED_ORDER)
  call TriggerAddAction(t,function P287UnitImmediate)
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_P287Unit,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerAddAction(t,function P287UnitPoint)
  if action==2 or action==3 then
   call IssueImmediateOrder(udg_P287Unit,"defend")
  endif
  if action==1 or action==3 or action==4 or action==15 or action==16 then
   call IssuePointOrder(udg_P287Unit,"move",1280,656)
  endif
  call P287Mark("scene-start")
 elseif phase==1 then
  call SetUnitX(udg_P287Unit,656)
  call SetUnitY(udg_P287Unit,656)
  set udg_P287Record=true
  call P287Mark("before")
  call PauseUnit(udg_P287Unit,true)
  call P287Mark("paused")
  if action==4 then
   call PauseUnit(udg_P287Unit,true)
   call P287Mark("paused-again")
  endif
 elseif phase==4 then
  call P287Mark("waiting")
  call PauseUnit(udg_P287Unit,false)
  call P287Mark("unpause-return")
  if action==4 then
   call PauseUnit(udg_P287Unit,false)
   call P287Mark("unpause-again")
  endif
 elseif phase==5 then
  call P287Mark("resumed")
 elseif phase==8 then
  call P287Mark("settled")
  set udg_P287Record=false
  call RemoveUnit(udg_P287Unit)
 elseif phase==9 then
  set udg_P287Scene=udg_P287Scene+1
  if udg_P287Scene==20 then
   call P287Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_P287Timer)
  endif
 endif
 set udg_P287Tick=udg_P287Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetPlayerTechResearched(Player(0),'Rhde',1)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(t,function P287PlayerImmediate)
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(t,function P287PlayerPoint)
 set udg_P287Timer=CreateTimer()
 call TimerStart(udg_P287Timer,0.1,true,function P287Tick)
 call P287Mark("selector-"+I2S(OrderId("stunned")))
 call P287Mark("start")
endfunction
