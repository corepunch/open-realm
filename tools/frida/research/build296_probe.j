// Undead summon work timing and independent worker/structure interruption.
globals
 unit udg_B296Worker=null
 unit udg_B296Building=null
 timer udg_B296Timer=null
 integer udg_B296Tick=0
 integer udg_B296Scene=0
 integer udg_B296Start=-1
endglobals
function B296Mark takes string label returns nothing
 local integer wp=0
 local integer bp=0
 if IsUnitPaused(udg_B296Worker) then
  set wp=1
 endif
 if IsUnitPaused(udg_B296Building) then
  set bp=1
 endif
 call Preload("B296 tick="+I2S(udg_B296Tick)+" scene="+I2S(udg_B296Scene)+" label="+label+" start="+I2S(udg_B296Start)+" order="+I2S(GetUnitCurrentOrder(udg_B296Worker))+" wp="+I2S(wp)+" bp="+I2S(bp)+" worker="+I2S(GetUnitTypeId(udg_B296Worker))+" building="+I2S(GetUnitTypeId(udg_B296Building))+" life="+R2S(GetWidgetLife(udg_B296Building))+" x="+R2S(GetUnitX(udg_B296Worker))+" y="+R2S(GetUnitY(udg_B296Worker)))
endfunction
function B296Start takes nothing returns nothing
 set udg_B296Building=GetConstructingStructure()
 set udg_B296Start=udg_B296Tick
 call B296Mark("construct-start")
endfunction
function B296Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_B296Tick,100)
 local boolean ok=false
 if t==0 then
  set udg_B296Start=-1
  set udg_B296Building=null
  set udg_B296Worker=CreateUnit(Player(0),'uW96',640,1024,0)
  call SetUnitAcquireRange(udg_B296Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,100)
  if udg_B296Scene==1 then
   set ok=IssueBuildOrderById(udg_B296Worker,'uC96',768,1024)
  else
   set ok=IssueBuildOrderById(udg_B296Worker,'uB96',768,1024)
  endif
  if ok then
   call B296Mark("accepted")
  else
   call B296Mark("rejected")
  endif
 endif
 if udg_B296Start>=0 then
  if udg_B296Tick==udg_B296Start+4 then
   if udg_B296Scene==2 then
    call PauseUnit(udg_B296Worker,true)
   elseif udg_B296Scene==3 then
    call PauseUnit(udg_B296Building,true)
   elseif udg_B296Scene==4 then
    set ok=IssuePointOrder(udg_B296Worker,"move",1408,1024)
   elseif udg_B296Scene==5 then
    call RemoveUnit(udg_B296Building)
   elseif udg_B296Scene==6 then
    set ok=IssueImmediateOrder(udg_B296Worker,"stop")
   elseif udg_B296Scene==7 then
    call KillUnit(udg_B296Building)
   endif
   call B296Mark("action")
  elseif udg_B296Tick==udg_B296Start+28 then
   if udg_B296Scene==2 then
    call PauseUnit(udg_B296Worker,false)
   elseif udg_B296Scene==3 then
    call PauseUnit(udg_B296Building,false)
   endif
   call B296Mark("resume")
  endif
 endif
 if t==95 then
  call RemoveUnit(udg_B296Worker)
  call RemoveUnit(udg_B296Building)
 elseif t==99 then
  if udg_B296Scene==7 then
   call B296Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_B296Timer)
  endif
  set udg_B296Scene=udg_B296Scene+1
 endif
 if t<95 then
  call B296Mark("sample")
 endif
 set udg_B296Tick=udg_B296Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger started=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetBlight(Player(0),768,1024,512,true)
 call TriggerRegisterPlayerUnitEvent(started,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(started,function B296Start)
 set udg_B296Timer=CreateTimer()
 call TimerStart(udg_B296Timer,0.05,true,function B296Tick)
 call Preload("B296 tick=0 scene=0 label=start")
endfunction
