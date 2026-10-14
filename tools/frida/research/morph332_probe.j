globals
 timer udg_M332Timer=null
 unit udg_M332Unit=null
 integer udg_M332Tick=0
 integer udg_M332Scene=0
endglobals
function M332Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_M332Unit) then
  set paused=1
 endif
 call Preload("M332 tick="+I2S(udg_M332Tick)+" scene="+I2S(udg_M332Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M332Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M332Unit))+" paused="+I2S(paused))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function M332Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M332Tick,80)
 local integer form=udg_M332Scene/3
 local integer action=ModuloInteger(udg_M332Scene,3)
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_M332Unit=CreateUnit(Player(0),'ugar',640,1024,0)
   call UnitAddAbility(udg_M332Unit,'Astn')
  else
   set udg_M332Unit=CreateUnit(Player(0),'uobs',640,1024,0)
   call UnitAddAbility(udg_M332Unit,'Aave')
   call SetUnitAbilityLevel(udg_M332Unit,'Aave',1)
  endif
  call SetUnitAcquireRange(udg_M332Unit,0)
  call SetUnitState(udg_M332Unit,UNIT_STATE_MANA,1000)
  call M332Mark("created")
 elseif age==2 or (age==42 and form==0) then
  if form==0 then
   if age==2 then
    set ok=IssueImmediateOrder(udg_M332Unit,"stoneform")
   else
    set ok=IssueImmediateOrder(udg_M332Unit,"unstoneform")
   endif
  else
   if age==2 then
    set ok=IssueImmediateOrder(udg_M332Unit,"avengerform")
   else
    set ok=IssueImmediateOrder(udg_M332Unit,"unavengerform")
   endif
  endif
  if ok then
   call M332Mark("cast-accepted")
  else
   call M332Mark("cast-rejected")
  endif
 elseif age==3 or (age==43 and form==0) then
  if action==1 then
   set ok=IssueImmediateOrder(udg_M332Unit,"stop")
   if ok then
    call M332Mark("stop-accepted")
   else
    call M332Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_M332Unit,true)
   call M332Mark("pause")
  elseif action==3 then
   call KillUnit(udg_M332Unit)
   call M332Mark("kill")
  endif
 elseif (age==25 or age==65) and action==2 then
  call PauseUnit(udg_M332Unit,false)
  call M332Mark("unpause")
 elseif age==77 then
  call RemoveUnit(udg_M332Unit)
  call M332Mark("cleanup")
 elseif age==79 then
  if udg_M332Scene==5 then
   call M332Mark("complete")
   call PauseTimer(udg_M332Timer)
  endif
  set udg_M332Scene=udg_M332Scene+1
 endif
 if age<77 then
  call M332Mark("sample")
 endif
 set udg_M332Tick=udg_M332Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetPlayerTechResearched(Player(0),'Rusp',1)
 call SetPlayerTechResearched(Player(0),'Rugf',1)
 call SetPlayerTechResearched(Player(0),'Rudf',1)
 call SetRandomSeed(1332)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call M332Mark("start")
 set udg_M332Timer=CreateTimer()
 call TimerStart(udg_M332Timer,0.1,true,function M332Tick)
endfunction
