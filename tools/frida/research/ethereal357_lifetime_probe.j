globals
 timer udg_L357Timer=null
 unit udg_L357Unit=null
 integer udg_L357Tick=0
 integer udg_L357Scene=0
endglobals
function L357Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_L357Unit) then
  set paused=1
 endif
 call Preload("L357 tick="+I2S(udg_L357Tick)+" scene="+I2S(udg_L357Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_L357Unit))+" order="+I2S(GetUnitCurrentOrder(udg_L357Unit))+" paused="+I2S(paused))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function L357Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_L357Tick,80)
 local integer form=udg_L357Scene
 local integer action=0
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_L357Unit=CreateUnit(Player(0),'ospm',640,1024,0)
   call UnitAddAbility(udg_L357Unit,'Acpf')
   call UnitMakeAbilityPermanent(udg_L357Unit,true,'Acpf')
  else
   set udg_L357Unit=CreateUnit(Player(0),'ospw',640,1024,0)
   call UnitAddAbility(udg_L357Unit,'Aetf')
   call UnitMakeAbilityPermanent(udg_L357Unit,true,'Aetf')
   call SetUnitAbilityLevel(udg_L357Unit,'Aetf',1)
  endif
  call SetUnitAcquireRange(udg_L357Unit,0)
  call SetUnitState(udg_L357Unit,UNIT_STATE_MANA,1000)
  call L357Mark("created")
 elseif age==2 or age==42 then
  if form==0 then
   if age==2 then
    set ok=IssueImmediateOrder(udg_L357Unit,"corporealform")
   else
    set ok=IssueImmediateOrder(udg_L357Unit,"uncorporealform")
   endif
  else
   if age==2 then
    set ok=IssueImmediateOrder(udg_L357Unit,"etherealform")
   else
    set ok=IssueImmediateOrder(udg_L357Unit,"unetherealform")
   endif
  endif
  if ok then
   call L357Mark("cast-accepted")
  else
   call L357Mark("cast-rejected")
  endif
 elseif age==3 or age==43 then
  if action==1 then
   set ok=IssueImmediateOrder(udg_L357Unit,"stop")
   if ok then
    call L357Mark("stop-accepted")
   else
    call L357Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_L357Unit,true)
   call L357Mark("pause")
  elseif action==3 then
   call KillUnit(udg_L357Unit)
   call L357Mark("kill")
  endif
 elseif (age==25 or age==65) and action==2 then
  call PauseUnit(udg_L357Unit,false)
  call L357Mark("unpause")
 elseif age==77 then
  call RemoveUnit(udg_L357Unit)
  call L357Mark("cleanup")
 elseif age==79 then
  if udg_L357Scene==1 then
   call L357Mark("complete")
   call PauseTimer(udg_L357Timer)
  endif
  set udg_L357Scene=udg_L357Scene+1
 endif
 if age<77 then
  call L357Mark("sample")
 endif
 set udg_L357Tick=udg_L357Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetPlayerTechResearched(Player(0),'Ruba',1)
 call SetRandomSeed(1333)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call L357Mark("start")
 set udg_L357Timer=CreateTimer()
 call TimerStart(udg_L357Timer,0.1,true,function L357Tick)
endfunction
