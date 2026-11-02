globals
 timer udg_M331Timer=null
 unit udg_M331Unit=null
 integer udg_M331Tick=0
 integer udg_M331Scene=0
endglobals
function M331Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_M331Unit) then
  set paused=1
 endif
 call Preload("M331 tick="+I2S(udg_M331Tick)+" scene="+I2S(udg_M331Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M331Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M331Unit))+" paused="+I2S(paused))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function M331Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M331Tick,80)
 local integer form=udg_M331Scene/3
 local integer action=ModuloInteger(udg_M331Scene,3)
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_M331Unit=CreateUnit(Player(0),'ucry',640,1024,0)
   call UnitAddAbility(udg_M331Unit,'Abur')
  else
   set udg_M331Unit=CreateUnit(Player(0),'Ntin',640,1024,0)
   call UnitAddAbility(udg_M331Unit,'ANrg')
   call SetUnitAbilityLevel(udg_M331Unit,'ANrg',1)
  endif
  call SetUnitAcquireRange(udg_M331Unit,0)
  call SetUnitState(udg_M331Unit,UNIT_STATE_MANA,1000)
  call M331Mark("created")
 elseif age==2 or age==42 then
  if form==0 then
   if age==2 then
    set ok=IssueImmediateOrder(udg_M331Unit,"burrow")
   else
    set ok=IssueImmediateOrder(udg_M331Unit,"unburrow")
   endif
  else
   if age==2 then
    set ok=IssueImmediateOrder(udg_M331Unit,"robogoblin")
   else
    set ok=IssueImmediateOrder(udg_M331Unit,"unrobogoblin")
   endif
  endif
  if ok then
   call M331Mark("cast-accepted")
  else
   call M331Mark("cast-rejected")
  endif
 elseif age==3 or age==43 then
  if action==1 then
   set ok=IssueImmediateOrder(udg_M331Unit,"stop")
   if ok then
    call M331Mark("stop-accepted")
   else
    call M331Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_M331Unit,true)
   call M331Mark("pause")
  elseif action==3 then
   call KillUnit(udg_M331Unit)
   call M331Mark("kill")
  endif
 elseif (age==25 or age==65) and action==2 then
  call PauseUnit(udg_M331Unit,false)
  call M331Mark("unpause")
 elseif age==77 then
  call RemoveUnit(udg_M331Unit)
  call M331Mark("cleanup")
 elseif age==79 then
  if udg_M331Scene==5 then
   call M331Mark("complete")
   call PauseTimer(udg_M331Timer)
  endif
  set udg_M331Scene=udg_M331Scene+1
 endif
 if age<77 then
  call M331Mark("sample")
 endif
 set udg_M331Tick=udg_M331Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetPlayerTechResearched(Player(0),'Ruba',1)
 call SetRandomSeed(1331)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call M331Mark("start")
 set udg_M331Timer=CreateTimer()
 call TimerStart(udg_M331Timer,0.1,true,function M331Tick)
endfunction
