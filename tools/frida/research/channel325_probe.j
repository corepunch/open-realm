globals
 timer udg_C325Timer=null
 unit udg_C325Unit=null
 integer udg_C325Tick=0
 integer udg_C325Scene=0
 integer udg_C325Ability=0
endglobals
function C325Bool takes boolean value returns integer
 if value then
  return 1
 endif
 return 0
endfunction
function C325Mark takes string label returns nothing
 call Preload("C325 tick="+I2S(udg_C325Tick)+" scene="+I2S(udg_C325Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_C325Unit))+" paused="+I2S(C325Bool(IsUnitPaused(udg_C325Unit)))+" x="+R2S(GetUnitX(udg_C325Unit))+" y="+R2S(GetUnitY(udg_C325Unit)))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function C325Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_C325Tick,45)
 local integer action=ModuloInteger(udg_C325Scene,10)
 local boolean ok=false
 if age==0 then
  set udg_C325Unit=CreateUnit(Player(0),'hfoo',640,1024,0)
  call SetUnitAcquireRange(udg_C325Unit,0)
  if udg_C325Scene<10 then
   set udg_C325Ability='ASch'
  else
   set udg_C325Ability='ASc1'
  endif
  call UnitAddAbility(udg_C325Unit,udg_C325Ability)
  call C325Mark("created")
 elseif age==2 then
  set ok=IssueImmediateOrder(udg_C325Unit,"channel")
  if ok then
   call C325Mark("cast-accepted")
  else
   call C325Mark("cast-rejected")
  endif
 elseif (age==3 and (action==1 or action==2)) or (age==10 and (action==3 or action==4)) then
  if action==1 or action==3 then
   set ok=IssueImmediateOrder(udg_C325Unit,"stop")
  else
   set ok=IssuePointOrder(udg_C325Unit,"move",896,1024)
  endif
  if ok then
   call C325Mark("replacement-accepted")
  else
   call C325Mark("replacement-rejected")
  endif
 elseif age==10 then
  if action==5 then
   call PauseUnit(udg_C325Unit,true)
   call C325Mark("pause")
  elseif action==6 then
   call KillUnit(udg_C325Unit)
   call C325Mark("kill")
  elseif action==7 then
   call UnitRemoveAbility(udg_C325Unit,udg_C325Ability)
   call C325Mark("remove-ability")
  elseif action==8 then
   call RemoveUnit(udg_C325Unit)
   call C325Mark("remove-unit")
  elseif action==9 then
   set ok=IssueImmediateOrder(udg_C325Unit,"channel")
   if ok then
    call C325Mark("recast-accepted")
   else
    call C325Mark("recast-rejected")
   endif
  endif
 elseif age==@PAUSE_END@ and action==5 then
  call PauseUnit(udg_C325Unit,false)
  call C325Mark("unpause")
 elseif age==42 then
  call RemoveUnit(udg_C325Unit)
  call C325Mark("cleanup")
 elseif age==44 then
  if udg_C325Scene==19 then
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
