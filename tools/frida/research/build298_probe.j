// Human Build -> transient Repair notification, queue and cancellation.
globals
 unit udg_B298Worker=null
 unit udg_B298Building=null
 timer udg_B298Timer=null
 integer udg_B298Tick=0
 integer udg_B298Scene=0
 integer udg_B298Start=-1
endglobals
function B298Mark takes string label returns nothing
 local integer wp=0
 if IsUnitPaused(udg_B298Worker) then
  set wp=1
 endif
 call Preload("B298 tick="+I2S(udg_B298Tick)+" scene="+I2S(udg_B298Scene)+" label="+label+" start="+I2S(udg_B298Start)+" order="+I2S(GetUnitCurrentOrder(udg_B298Worker))+" wp="+I2S(wp)+" worker="+I2S(GetUnitTypeId(udg_B298Worker))+" building="+I2S(GetUnitTypeId(udg_B298Building))+" life="+R2S(GetWidgetLife(udg_B298Building))+" x="+R2S(GetUnitX(udg_B298Worker))+" y="+R2S(GetUnitY(udg_B298Worker)))
endfunction
function B298Started takes nothing returns nothing
 set udg_B298Building=GetConstructingStructure()
 set udg_B298Start=udg_B298Tick
 call B298Mark("construct-start")
endfunction
function B298Finished takes nothing returns nothing
 call B298Mark("construct-finish")
endfunction
function B298Target takes nothing returns nothing
 local boolean ok=false
 call B298Mark("issued-target-"+I2S(GetIssuedOrderId()))
 if GetIssuedOrderId()==OrderId("repair") then
  if udg_B298Scene==6 then
   set ok=IssuePointOrder(udg_B298Worker,"move",1408,1024)
   call B298Mark("callback-move")
  elseif udg_B298Scene==7 then
   set ok=IssueImmediateOrder(udg_B298Worker,"stop")
   call B298Mark("callback-stop")
  elseif udg_B298Scene==8 then
   call RemoveUnit(udg_B298Building)
   call B298Mark("callback-remove-building")
  elseif udg_B298Scene==9 then
   call RemoveUnit(udg_B298Worker)
   call B298Mark("callback-remove-worker")
  endif
 endif
endfunction
function B298Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_B298Tick,100)
 local boolean ok=false
 if t==0 then
  set udg_B298Start=-1
  set udg_B298Building=null
  set udg_B298Worker=CreateUnit(Player(0),'hW98',640,1024,0)
  call SetUnitAcquireRange(udg_B298Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,100)
  set ok=IssueBuildOrderById(udg_B298Worker,'hB98',768,1024)
  if ok then
   call B298Mark("accepted")
  else
   call B298Mark("rejected")
  endif
 endif
 if udg_B298Start>=0 then
  if udg_B298Tick==udg_B298Start+4 then
   if udg_B298Scene==1 then
    set ok=IssuePointOrder(udg_B298Worker,"move",1408,1024)
   elseif udg_B298Scene==2 then
    set ok=IssueImmediateOrder(udg_B298Worker,"stop")
   elseif udg_B298Scene==3 then
    call PauseUnit(udg_B298Worker,true)
   elseif udg_B298Scene==4 then
    call KillUnit(udg_B298Building)
   elseif udg_B298Scene==5 then
    call RemoveUnit(udg_B298Building)
   endif
   call B298Mark("action")
  elseif udg_B298Tick==udg_B298Start+28 then
   if udg_B298Scene==3 then
    call PauseUnit(udg_B298Worker,false)
   endif
   call B298Mark("resume")
  endif
 endif
 if t==95 then
  call RemoveUnit(udg_B298Worker)
  call RemoveUnit(udg_B298Building)
 elseif t==99 then
  if udg_B298Scene==9 then
   call B298Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_B298Timer)
  endif
  set udg_B298Scene=udg_B298Scene+1
 endif
 if t<95 then
  call B298Mark("sample")
 endif
 set udg_B298Tick=udg_B298Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger started=CreateTrigger()
 local trigger finished=CreateTrigger()
 local trigger target=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1028)
 call FogEnable(false)
 call FogMaskEnable(false)
 call TriggerRegisterPlayerUnitEvent(started,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(started,function B298Started)
 call TriggerRegisterPlayerUnitEvent(finished,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,null)
 call TriggerAddAction(finished,function B298Finished)
 call TriggerRegisterPlayerUnitEvent(target,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddAction(target,function B298Target)
 set udg_B298Timer=CreateTimer()
 call B298Mark("start")
 call TimerStart(udg_B298Timer,0.05,true,function B298Tick)
endfunction
