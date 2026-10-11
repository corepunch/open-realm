globals
 unit udg_S284Unit=null
 timer udg_S284Timer=null
 integer udg_S284Tick=0
 integer udg_S284Scene=0
 boolean udg_S284Nested=false
endglobals
function S284Mark takes string label returns nothing
 call Preload("S284 tick="+I2S(udg_S284Tick)+" scene="+I2S(udg_S284Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S284Unit))+" x="+R2S(GetUnitX(udg_S284Unit))+" y="+R2S(GetUnitY(udg_S284Unit)))
endfunction
function S284Terrain takes boolean enabled returns nothing
 local integer x=19
 local integer y=19
 loop
  exitwhen y>21
  set x=19
  loop
   exitwhen x>21
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,enabled)
   set x=x+1
  endloop
  set y=y+1
 endloop
endfunction
function S284Player takes nothing returns nothing
 local integer action=ModuloInteger(udg_S284Scene,8)
 if GetTriggerUnit()!=udg_S284Unit or GetIssuedOrderId()!=OrderId("stop") then
  return
 endif
 call S284Mark("player-enter")
 if not udg_S284Nested then
  set udg_S284Nested=true
  if action==1 then
   call S284Terrain(false)
  elseif action==2 then
   call IssuePointOrder(udg_S284Unit,"move",1200,656)
  elseif action==3 then
   call IssueImmediateOrder(udg_S284Unit,"stop")
  elseif action==4 then
   call RemoveUnit(udg_S284Unit)
  elseif action==5 then
   call PauseUnit(udg_S284Unit,true)
  elseif action==6 then
   call KillUnit(udg_S284Unit)
  elseif action==7 then
   call SetUnitPosition(udg_S284Unit,800,800)
  endif
 endif
 call S284Mark("player-exit")
endfunction
function S284Unit takes nothing returns nothing
 if GetIssuedOrderId()==OrderId("stop") then
  call S284Mark("unit-event")
 endif
endfunction
function S284Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_S284Tick,10)
 local trigger t=null
 if phase==0 then
  set udg_S284Nested=false
  set udg_S284Unit=CreateUnit(Player(0),'hfoo',656,656,0)
  call SetUnitAcquireRange(udg_S284Unit,0)
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_S284Unit,EVENT_UNIT_ISSUED_ORDER)
  call TriggerAddAction(t,function S284Unit)
  if udg_S284Scene>=8 then
   call IssuePointOrder(udg_S284Unit,"move",1200,656)
  endif
 elseif phase==1 then
  if udg_S284Scene>=8 then
   call SetUnitPathing(udg_S284Unit,false)
   call SetUnitX(udg_S284Unit,656)
   call SetUnitY(udg_S284Unit,656)
   call SetUnitPathing(udg_S284Unit,true)
  endif
  call S284Mark("before")
  call IssueImmediateOrder(udg_S284Unit,"stop")
  call S284Mark("after")
 elseif phase==4 then
  call S284Mark("settled")
  call PauseUnit(udg_S284Unit,false)
  call RemoveUnit(udg_S284Unit)
  call S284Terrain(true)
 elseif phase==9 then
  set udg_S284Scene=udg_S284Scene+1
  if udg_S284Scene==16 then
   call S284Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_S284Timer)
  endif
 endif
 set udg_S284Tick=udg_S284Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(t,function S284Player)
 set udg_S284Timer=CreateTimer()
 call TimerStart(udg_S284Timer,0.1,true,function S284Tick)
 call S284Mark("start")
endfunction
