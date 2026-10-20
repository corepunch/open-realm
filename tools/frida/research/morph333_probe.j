globals
 timer udg_M333Timer=null
 unit udg_M333Unit=null
 integer udg_M333Tick=0
 integer udg_M333Scene=0
endglobals
function M333Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_M333Unit) then
  set paused=1
 endif
 call Preload("M333 tick="+I2S(udg_M333Tick)+" scene="+I2S(udg_M333Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M333Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M333Unit))+" paused="+I2S(paused))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function M333Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M333Tick,80)
 local integer form=udg_M333Scene/3
 local integer action=ModuloInteger(udg_M333Scene,3)
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_M333Unit=CreateUnit(Player(0),'ospm',640,1024,0)
   call UnitAddAbility(udg_M333Unit,'Acpf')
   call UnitMakeAbilityPermanent(udg_M333Unit,true,'Acpf')
  else
   set udg_M333Unit=CreateUnit(Player(0),'ospw',640,1024,0)
   call UnitAddAbility(udg_M333Unit,'Aetf')
   call UnitMakeAbilityPermanent(udg_M333Unit,true,'Aetf')
   call SetUnitAbilityLevel(udg_M333Unit,'Aetf',1)
  endif
  call SetUnitAcquireRange(udg_M333Unit,0)
  call SetUnitState(udg_M333Unit,UNIT_STATE_MANA,1000)
  call M333Mark("created")
 elseif age==2 or age==42 then
  if form==0 then
   if age==2 then
    set ok=IssueImmediateOrder(udg_M333Unit,"corporealform")
   else
    set ok=IssueImmediateOrder(udg_M333Unit,"uncorporealform")
   endif
  else
   if age==2 then
    set ok=IssueImmediateOrder(udg_M333Unit,"etherealform")
   else
    set ok=IssueImmediateOrder(udg_M333Unit,"unetherealform")
   endif
  endif
  if ok then
   call M333Mark("cast-accepted")
  else
   call M333Mark("cast-rejected")
  endif
 elseif age==3 or age==43 then
  if action==1 then
   set ok=IssueImmediateOrder(udg_M333Unit,"stop")
   if ok then
    call M333Mark("stop-accepted")
   else
    call M333Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_M333Unit,true)
   call M333Mark("pause")
  elseif action==3 then
   call KillUnit(udg_M333Unit)
   call M333Mark("kill")
  endif
 elseif (age==25 or age==65) and action==2 then
  call PauseUnit(udg_M333Unit,false)
  call M333Mark("unpause")
 elseif age==77 then
  call RemoveUnit(udg_M333Unit)
  call M333Mark("cleanup")
 elseif age==79 then
  if udg_M333Scene==5 then
   call M333Mark("complete")
   call PauseTimer(udg_M333Timer)
  endif
  set udg_M333Scene=udg_M333Scene+1
 endif
 if age<77 then
  call M333Mark("sample")
 endif
 set udg_M333Tick=udg_M333Tick+1
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
 call M333Mark("start")
 set udg_M333Timer=CreateTimer()
 call TimerStart(udg_M333Timer,0.1,true,function M333Tick)
endfunction
