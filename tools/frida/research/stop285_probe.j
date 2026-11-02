globals
 unit udg_S285Unit=null
 timer udg_S285Timer=null
 integer udg_S285Tick=0
 integer udg_S285Scene=0
 boolean udg_S285Nested=false
endglobals
function S285Mark takes string label returns nothing
 call Preload("S285 tick="+I2S(udg_S285Tick)+" scene="+I2S(udg_S285Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S285Unit))+" x="+R2S(GetUnitX(udg_S285Unit))+" y="+R2S(GetUnitY(udg_S285Unit)))
endfunction
function S285Terrain takes boolean enabled returns nothing
 local integer x=24
 local integer y=24
 loop
  exitwhen y>26
  set x=24
  loop
   exitwhen x>26
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,enabled)
   set x=x+1
  endloop
  set y=y+1
 endloop
endfunction
function S285Player takes nothing returns nothing
 local integer action=ModuloInteger(udg_S285Scene,8)
 if GetTriggerUnit()!=udg_S285Unit or GetIssuedOrderId()!=OrderId("stop") then
  return
 endif
 call S285Mark("player-enter")
 if not udg_S285Nested then
  set udg_S285Nested=true
  if action==1 then
   call S285Terrain(false)
  elseif action==2 then
   call IssuePointOrder(udg_S285Unit,"move",1200,656)
  elseif action==3 then
   call IssueImmediateOrder(udg_S285Unit,"stop")
  elseif action==4 then
   call RemoveUnit(udg_S285Unit)
  elseif action==5 then
   call PauseUnit(udg_S285Unit,true)
  elseif action==6 then
   call KillUnit(udg_S285Unit)
  elseif action==7 then
   call SetUnitX(udg_S285Unit,900)
  endif
 endif
 call S285Mark("player-exit")
endfunction
function S285Unit takes nothing returns nothing
 if GetIssuedOrderId()==OrderId("stop") then
  call S285Mark("unit-event")
 endif
endfunction
function S285Moving takes nothing returns boolean
 return (udg_S285Scene<32 and ModuloInteger(udg_S285Scene/8,2)==1) or udg_S285Scene==33 or udg_S285Scene==35
endfunction
function S285Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_S285Tick,10)
 local trigger t=null
 local location p=null
 if phase==0 then
  set udg_S285Nested=false
  set udg_S285Unit=CreateUnit(Player(0),'hfoo',656,656,0)
  call SetUnitAcquireRange(udg_S285Unit,0)
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_S285Unit,EVENT_UNIT_ISSUED_ORDER)
  call TriggerAddAction(t,function S285Unit)
  if S285Moving() then
   call IssuePointOrder(udg_S285Unit,"move",1200,656)
  endif
 elseif phase==1 then
  if S285Moving() then
   call SetUnitPathing(udg_S285Unit,false)
   call SetUnitX(udg_S285Unit,656)
   call SetUnitY(udg_S285Unit,656)
   call SetUnitPathing(udg_S285Unit,true)
  endif
  if udg_S285Scene>=32 then
   call PauseUnit(udg_S285Unit,true)
  endif
  call S285Mark("before")
  if udg_S285Scene<16 or (udg_S285Scene>=32 and udg_S285Scene<34) then
   call SetUnitPosition(udg_S285Unit,800,800)
  else
   set p=Location(800,800)
   call SetUnitPositionLoc(udg_S285Unit,p)
   call RemoveLocation(p)
  endif
  call S285Mark("after")
 elseif phase==4 then
  call S285Mark("settled")
  call PauseUnit(udg_S285Unit,false)
  if udg_S285Scene<32 then
   call RemoveUnit(udg_S285Unit)
  endif
  call S285Terrain(true)
 elseif phase==7 and udg_S285Scene>=32 then
  call S285Mark("resumed")
 elseif phase==8 and udg_S285Scene>=32 then
  call RemoveUnit(udg_S285Unit)
 elseif phase==9 then
  set udg_S285Scene=udg_S285Scene+1
  if udg_S285Scene==36 then
   call S285Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_S285Timer)
  endif
 endif
 set udg_S285Tick=udg_S285Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(t,function S285Player)
 set udg_S285Timer=CreateTimer()
 call TimerStart(udg_S285Timer,0.1,true,function S285Tick)
 call S285Mark("start")
endfunction
