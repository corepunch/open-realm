// Public Build current-head ownership across five construction strategies.
globals
 unit udg_B295Worker=null
 unit udg_B295Building=null
 timer udg_B295Timer=null
 integer udg_B295Tick=0
 integer udg_B295Scene=0
 integer array udg_B295Workers
 integer array udg_B295Buildings
 boolean udg_B295Nested=false
endglobals
function B295Mark takes string label returns nothing
 local integer hidden=0
 if IsUnitHidden(udg_B295Worker) then
  set hidden=1
 endif
 call Preload("B295 tick="+I2S(udg_B295Tick)+" scene="+I2S(udg_B295Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_B295Worker))+" hidden="+I2S(hidden)+" life="+R2S(GetWidgetLife(udg_B295Worker))+" building="+I2S(GetUnitTypeId(udg_B295Building))+" buildlife="+R2S(GetWidgetLife(udg_B295Building))+" worker="+I2S(GetUnitTypeId(udg_B295Worker)))
endfunction
function B295Start takes nothing returns nothing
 local integer action=udg_B295Scene/5
 local boolean ok=false
 set udg_B295Building=GetConstructingStructure()
 call B295Mark("construct-start")
 if action==1 then
  set ok=IssuePointOrder(udg_B295Worker,"move",1408,1024)
 elseif action==2 then
  set ok=IssueImmediateOrder(udg_B295Worker,"stop")
 elseif action==3 then
  call RemoveUnit(udg_B295Building)
 else
  if action==4 then
   call RemoveUnit(udg_B295Worker)
  endif
 endif
 if action>0 then
  if ok then
   call B295Mark("callback-accepted")
  else
   call B295Mark("callback-return")
  endif
 endif
endfunction
function B295Done takes nothing returns nothing
 call B295Mark("construct-finish")
endfunction
function B295Order takes nothing returns nothing
 if GetOrderedUnit()!=udg_B295Worker then
  return
 endif
 call B295Mark("point-"+I2S(GetIssuedOrderId()))

endfunction
function B295Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_B295Tick,45)
 local integer kind=ModuloInteger(udg_B295Scene,5)
 local boolean ok=false
 if t==0 then
  set udg_B295Building=null
  set udg_B295Worker=CreateUnit(Player(0),udg_B295Workers[kind],640,1024,0)
  call SetUnitAcquireRange(udg_B295Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,100)
  call B295Mark("created")
 elseif t==1 then
  call IssuePointOrder(udg_B295Worker,"move",512,1024)
  call B295Mark("prior-move")
 elseif t==3 then
  set ok=IssueBuildOrderById(udg_B295Worker,udg_B295Buildings[kind],768,1024)
  if ok then
   call B295Mark("build-accepted")
  else
   call B295Mark("build-rejected")
  endif
 elseif t==5 then
  set ok=IssueBuildOrderById(udg_B295Worker,'hfoo',768,1024)
  if ok then
   call B295Mark("bad-build-accepted")
  else
   call B295Mark("bad-build-rejected")
  endif
 elseif t==42 then
  call RemoveUnit(udg_B295Worker)
  call RemoveUnit(udg_B295Building)
 elseif t==44 then
  if udg_B295Scene==24 then
   call B295Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_B295Timer)
  endif
  set udg_B295Scene=udg_B295Scene+1
 endif
 if t<42 then
  call B295Mark("sample")
 endif
 set udg_B295Tick=udg_B295Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger started=CreateTrigger()
 local trigger done=CreateTrigger()
 local trigger orders=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetBlight(Player(0),768,1024,512,true)
 set udg_B295Workers[0]='hW95'
 set udg_B295Workers[1]='oW95'
 set udg_B295Workers[2]='uW95'
 set udg_B295Workers[3]='eW95'
 set udg_B295Workers[4]='nW95'
 set udg_B295Buildings[0]='hB95'
 set udg_B295Buildings[1]='oB95'
 set udg_B295Buildings[2]='uB95'
 set udg_B295Buildings[3]='eB95'
 set udg_B295Buildings[4]='nB95'
 call TriggerRegisterPlayerUnitEvent(started,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(started,function B295Start)
 call TriggerRegisterPlayerUnitEvent(done,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,null)
 call TriggerAddAction(done,function B295Done)
 call TriggerRegisterPlayerUnitEvent(orders,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(orders,function B295Order)
 set udg_B295Timer=CreateTimer()
 call TimerStart(udg_B295Timer,0.1,true,function B295Tick)
 call Preload("B295 tick=0 scene=0 label=start")
endfunction
