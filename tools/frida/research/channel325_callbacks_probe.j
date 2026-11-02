globals
 timer udg_C325Timer=null
 unit udg_C325Unit=null
 integer udg_C325Tick=0
 integer udg_C325Scene=0
 boolean udg_C325Fired=false
endglobals
function C325Mark takes string label returns nothing
 call Preload("C325 tick="+I2S(udg_C325Tick)+" scene="+I2S(udg_C325Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_C325Unit)))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function C325Event takes nothing returns nothing
 local integer phase=-1
 local boolean ok=false
 if GetTriggerUnit()!=udg_C325Unit or udg_C325Fired then
  return
 endif
 if GetTriggerEventId()==EVENT_PLAYER_UNIT_SPELL_CHANNEL then
  set phase=0
 elseif GetTriggerEventId()==EVENT_PLAYER_UNIT_SPELL_CAST then
  set phase=1
 elseif GetTriggerEventId()==EVENT_PLAYER_UNIT_SPELL_EFFECT then
  set phase=2
 elseif GetTriggerEventId()==EVENT_PLAYER_UNIT_SPELL_FINISH then
  set phase=3
 endif
 if phase!=udg_C325Scene/2 then
  return
 endif
 set udg_C325Fired=true
 call C325Mark("callback-before")
 if ModuloInteger(udg_C325Scene,2)==0 then
  set ok=IssueImmediateOrder(udg_C325Unit,"stop")
 else
  set ok=IssueImmediateOrder(udg_C325Unit,"@RECAST_ORDER@")
 endif
 if ok then
  call C325Mark("callback-accepted")
 else
  call C325Mark("callback-rejected")
 endif
endfunction
function C325Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_C325Tick,45)
 local boolean ok=false
 if age==0 then
  set udg_C325Unit=CreateUnit(Player(0),'hfoo',640,1024,0)
  call SetUnitAcquireRange(udg_C325Unit,0)
  call UnitAddAbility(udg_C325Unit,'ASc1')
  if @CROSS_CHANNEL@ then
   call UnitAddAbility(udg_C325Unit,'ASc2')
  endif
  set udg_C325Fired=false
  call C325Mark("created")
 elseif age==2 then
  set ok=IssueImmediateOrder(udg_C325Unit,"channel")
  if ok then
   call C325Mark("cast-accepted")
  else
   call C325Mark("cast-rejected")
  endif
 elseif age==42 then
  call RemoveUnit(udg_C325Unit)
  call C325Mark("cleanup")
 elseif age==44 then
  if udg_C325Scene==7 then
   call C325Mark("complete")
   call PauseTimer(udg_C325Timer)
  endif
  set udg_C325Scene=udg_C325Scene+1
 endif
 if age<42 then
  call C325Mark("sample")
 endif
 set udg_C325Tick=udg_C325Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_SPELL_CAST,null)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_SPELL_EFFECT,null)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_SPELL_FINISH,null)
 call TriggerAddAction(t,function C325Event)
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1325)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call C325Mark("start")
 set udg_C325Timer=CreateTimer()
 call TimerStart(udg_C325Timer,0.1,true,function C325Tick)
endfunction
