// Haunted Mine construction completion and retained builder ownership.
globals
 unit udg_B297Worker=null
 unit udg_B297Building=null
 unit udg_B297Mine=null
 unit udg_B297Extra=null
 timer udg_B297Timer=null
 integer udg_B297Tick=0
 integer udg_B297Scene=0
 integer udg_B297Start=-1
endglobals
function B297Mark takes string label returns nothing
 local integer wp=0
 if IsUnitPaused(udg_B297Worker) then
  set wp=1
 endif
 call Preload("B297 tick="+I2S(udg_B297Tick)+" scene="+I2S(udg_B297Scene)+" label="+label+" start="+I2S(udg_B297Start)+" order="+I2S(GetUnitCurrentOrder(udg_B297Worker))+" wp="+I2S(wp)+" worker="+I2S(GetUnitTypeId(udg_B297Worker))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_B297Worker)))+" building="+I2S(GetUnitTypeId(udg_B297Building))+" life="+R2S(GetWidgetLife(udg_B297Building))+" x="+R2S(GetUnitX(udg_B297Worker))+" y="+R2S(GetUnitY(udg_B297Worker)))
endfunction
function B297Start takes nothing returns nothing
 if GetUnitTypeId(GetConstructingStructure())=='uB97' then
  set udg_B297Building=GetConstructingStructure()
  set udg_B297Start=udg_B297Tick
  call B297Mark("construct-start")
 else
  set udg_B297Extra=GetConstructingStructure()
  call B297Mark("extra-start")
 endif
endfunction
function B297Finish takes nothing returns nothing
 local boolean ok=false
 if GetConstructedStructure()==udg_B297Building then
  call B297Mark("construct-finish")
  if udg_B297Scene==11 then
   set ok=IssuePointOrder(udg_B297Worker,"move",1408,1024)
   if ok then
    call B297Mark("finish-move-accepted")
   else
    call B297Mark("finish-move-rejected")
   endif
  endif
 endif
endfunction
function B297Target takes nothing returns nothing
 if GetTriggerUnit()==udg_B297Worker then
  call B297Mark("issued-target-"+I2S(GetIssuedOrderId())+"-"+I2S(GetUnitTypeId(GetOrderTargetUnit())))
 endif
endfunction
function B297Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_B297Tick,160)
 local boolean ok=false
 local player p=Player(0)
 if t==0 then
  set udg_B297Start=-1
  set udg_B297Building=null
  set udg_B297Extra=null
  if udg_B297Scene==7 then
   set p=Player(1)
  endif
  set udg_B297Mine=CreateUnit(Player(15),'ngol',768,1024,0)
  call SetResourceAmount(udg_B297Mine,12500)
  set udg_B297Worker=CreateUnit(p,'uW97',512,1024,0)
  call SetUnitAcquireRange(udg_B297Worker,0)
  call SetPlayerState(p,PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(p,PLAYER_STATE_RESOURCE_LUMBER,100000)
  call SetPlayerState(p,PLAYER_STATE_RESOURCE_FOOD_CAP,100)
  set ok=IssueTargetOrderById(udg_B297Worker,'uB97',udg_B297Mine)
  if ok then
   call B297Mark("accepted")
  else
   call B297Mark("rejected")
  endif
 endif
 if udg_B297Start>=0 then
  if udg_B297Tick==udg_B297Start+4 then
   if udg_B297Scene==1 then
    set ok=IssuePointOrder(udg_B297Worker,"move",1408,1024)
   elseif udg_B297Scene==3 then
    set ok=IssueImmediateOrder(udg_B297Worker,"stop")
   elseif udg_B297Scene==4 then
    call PauseUnit(udg_B297Worker,true)
   elseif udg_B297Scene==5 then
    call RemoveUnit(udg_B297Building)
   elseif udg_B297Scene==6 then
    call KillUnit(udg_B297Building)
   elseif udg_B297Scene==10 then
    call RemoveUnit(udg_B297Worker)
   endif
   call B297Mark("early-action")
  elseif udg_B297Tick==udg_B297Start+28 then
   if udg_B297Scene==2 then
    set ok=IssuePointOrder(udg_B297Worker,"move",1408,1024)
   elseif udg_B297Scene==4 then
    call PauseUnit(udg_B297Worker,false)
   elseif udg_B297Scene==8 then
    set ok=IssueBuildOrderById(udg_B297Worker,'uZ97',1408,1024)
   elseif udg_B297Scene==9 then
    set ok=UnitRemoveAbility(udg_B297Building,'Abgm')
   endif
   call B297Mark("late-action")
  endif
 endif
 if t==155 then
  call RemoveUnit(udg_B297Worker)
  call RemoveUnit(udg_B297Building)
  call RemoveUnit(udg_B297Extra)
  call RemoveUnit(udg_B297Mine)
 elseif t==159 then
  if udg_B297Scene==11 then
   call B297Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_B297Timer)
  endif
  set udg_B297Scene=udg_B297Scene+1
 endif
 if t<155 then
  call B297Mark("sample")
 endif
 set udg_B297Tick=udg_B297Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger started=CreateTrigger()
 local trigger finished=CreateTrigger()
 local trigger targeted=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetBlight(Player(0),768,1024,512,true)
 call SetBlight(Player(1),768,1024,512,true)
 call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)
 call TriggerRegisterPlayerUnitEvent(started,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerRegisterPlayerUnitEvent(started,Player(1),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(started,function B297Start)
 call TriggerRegisterPlayerUnitEvent(finished,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,null)
 call TriggerRegisterPlayerUnitEvent(finished,Player(1),EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,null)
 call TriggerAddAction(finished,function B297Finish)
 call TriggerRegisterPlayerUnitEvent(targeted,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(targeted,Player(1),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddAction(targeted,function B297Target)
 set udg_B297Timer=CreateTimer()
 call TimerStart(udg_B297Timer,0.05,true,function B297Tick)
 call Preload("B297 tick=0 scene=0 label=start")
endfunction
