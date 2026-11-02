// Public Build current-head ownership across five construction strategies.
globals
 unit udg_B293Worker=null
 unit udg_B293Building=null
 timer udg_B293Timer=null
 integer udg_B293Tick=0
 integer udg_B293Scene=0
 integer array udg_B293Workers
 integer array udg_B293Buildings
 boolean udg_B293Nested=false
endglobals
function B293Mark takes string label returns nothing
 local integer hidden=0
 if IsUnitHidden(udg_B293Worker) then
  set hidden=1
 endif
 call Preload("B293 tick="+I2S(udg_B293Tick)+" scene="+I2S(udg_B293Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_B293Worker))+" hidden="+I2S(hidden)+" life="+R2S(GetWidgetLife(udg_B293Worker))+" building="+I2S(GetUnitTypeId(udg_B293Building)))
endfunction
function B293Start takes nothing returns nothing
 set udg_B293Building=GetConstructingStructure()
 call B293Mark("construct-start")
endfunction
function B293Done takes nothing returns nothing
 call B293Mark("construct-finish")
endfunction
function B293Order takes nothing returns nothing
 if GetOrderedUnit()!=udg_B293Worker then
  return
 endif
 call B293Mark("point-"+I2S(GetIssuedOrderId()))
 if udg_B293Scene==10 and not udg_B293Nested and GetIssuedOrderId()=='hB93' then
  set udg_B293Nested=true
  call IssuePointOrder(udg_B293Worker,"move",1408,1024)
  call B293Mark("nested-move-return")
 endif
endfunction
function B293Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_B293Tick,65)
 local integer kind=ModuloInteger(udg_B293Scene,5)
 local boolean ok=false
 if t==0 then
  set udg_B293Building=null
  set udg_B293Worker=CreateUnit(Player(0),udg_B293Workers[kind],640,1024,0)
  call SetUnitAcquireRange(udg_B293Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,100)
  call B293Mark("created")
 elseif t==1 then
  call IssuePointOrder(udg_B293Worker,"move",512,1024)
  call B293Mark("prior-move")
 elseif t==3 then
  set ok=IssueBuildOrderById(udg_B293Worker,udg_B293Buildings[kind],768,1024)
  if ok then
   call B293Mark("build-accepted")
  else
   call B293Mark("build-rejected")
  endif
 elseif t==5 then
  set ok=IssueBuildOrderById(udg_B293Worker,'hfoo',768,1024)
  if ok then
   call B293Mark("bad-build-accepted")
  else
   call B293Mark("bad-build-rejected")
  endif
 elseif t==15 and udg_B293Scene>=5 and udg_B293Scene<10 then
  set ok=IssuePointOrder(udg_B293Worker,"move",1408,1024)
  if ok then
   call B293Mark("replace-accepted")
  else
   call B293Mark("replace-rejected")
  endif
 elseif t==58 then
  call RemoveUnit(udg_B293Worker)
  call RemoveUnit(udg_B293Building)
 elseif t==64 then
  if udg_B293Scene==10 then
   call B293Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_B293Timer)
  endif
  set udg_B293Scene=udg_B293Scene+1
 endif
 if t<58 then
  call B293Mark("sample")
 endif
 set udg_B293Tick=udg_B293Tick+1
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
 set udg_B293Workers[0]='hW93'
 set udg_B293Workers[1]='oW93'
 set udg_B293Workers[2]='uW93'
 set udg_B293Workers[3]='eW93'
 set udg_B293Workers[4]='nW93'
 set udg_B293Buildings[0]='hB93'
 set udg_B293Buildings[1]='oB93'
 set udg_B293Buildings[2]='uB93'
 set udg_B293Buildings[3]='eB93'
 set udg_B293Buildings[4]='nB93'
 call TriggerRegisterPlayerUnitEvent(started,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(started,function B293Start)
 call TriggerRegisterPlayerUnitEvent(done,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,null)
 call TriggerAddAction(done,function B293Done)
 call TriggerRegisterPlayerUnitEvent(orders,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(orders,function B293Order)
 set udg_B293Timer=CreateTimer()
 call TimerStart(udg_B293Timer,0.1,true,function B293Tick)
 call Preload("B293 tick=0 scene=0 label=start")
endfunction
