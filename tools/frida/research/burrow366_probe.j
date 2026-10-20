globals
 timer udg_B366Timer=null
 unit udg_B366Unit=null
 integer udg_B366Tick=0
 integer udg_B366Scene=0
endglobals
function B366Mark takes string label returns nothing
 local integer invisible=0
 if IsUnitInvisible(udg_B366Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("B366 tick="+I2S(udg_B366Tick)+" scene="+I2S(udg_B366Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_B366Unit))+" order="+I2S(GetUnitCurrentOrder(udg_B366Unit))+" invisible="+I2S(invisible)+" apiv="+I2S(GetUnitAbilityLevel(udg_B366Unit,'Apiv')))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function B366Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_B366Tick,80)
 local boolean ok=false
 if age==0 then
  if udg_B366Scene==3 then
   set udg_B366Unit=CreateUnit(Player(0),'ucrm',640,1024,0)
  else
   set udg_B366Unit=CreateUnit(Player(0),'ucry',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_B366Unit,0)
  if udg_B366Scene==4 then
   call UnitAddAbility(udg_B366Unit,'Apiv')
   call UnitMakeAbilityPermanent(udg_B366Unit,true,'Apiv')
  endif
  call UnitAddAbility(udg_B366Unit,'Abur')
  call UnitMakeAbilityPermanent(udg_B366Unit,true,'Abur')
  call B366Mark("created")
 elseif age==2 or age==42 then
  if (age==2 and udg_B366Scene!=3) or (age==42 and udg_B366Scene==3) then
   set ok=IssueImmediateOrder(udg_B366Unit,"burrow")
  else
   set ok=IssueImmediateOrder(udg_B366Unit,"unburrow")
  endif
  if ok then
   call B366Mark("cast-accepted")
  else
   call B366Mark("cast-rejected")
  endif
 elseif age==3 or age==43 then
  if udg_B366Scene==1 then
   set ok=IssueImmediateOrder(udg_B366Unit,"stop")
   call B366Mark("stop")
  elseif udg_B366Scene==2 then
   call PauseUnit(udg_B366Unit,true)
   call B366Mark("pause")
  endif
 elseif age==20 then
  if udg_B366Scene==5 then
   call UnitAddAbility(udg_B366Unit,'Apiv')
   call UnitMakeAbilityPermanent(udg_B366Unit,true,'Apiv')
   call B366Mark("add-apiv")
  elseif udg_B366Scene==6 then
   call UnitRemoveAbility(udg_B366Unit,'Abur')
   call B366Mark("remove-burrow")
  endif
 elseif (age==25 or age==65) and udg_B366Scene==2 then
  call PauseUnit(udg_B366Unit,false)
  call B366Mark("unpause")
 elseif age==62 and (udg_B366Scene==4 or udg_B366Scene==5) then
  call UnitRemoveAbility(udg_B366Unit,'Apiv')
  call B366Mark("remove-apiv")
 elseif age==77 then
  call RemoveUnit(udg_B366Unit)
  call B366Mark("cleanup")
 elseif age==79 then
  if udg_B366Scene==6 then
   call B366Mark("complete")
   call PauseTimer(udg_B366Timer)
  endif
  set udg_B366Scene=udg_B366Scene+1
 endif
 if age<77 then
  call B366Mark("sample")
 endif
 set udg_B366Tick=udg_B366Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1366)
 call SetPlayerTechResearched(Player(0),'Rubu',1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call B366Mark("start")
 set udg_B366Timer=CreateTimer()
 call TimerStart(udg_B366Timer,0.1,true,function B366Tick)
endfunction
