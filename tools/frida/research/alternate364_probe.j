globals
 timer udg_A364Timer=null
 unit udg_A364Unit=null
 integer udg_A364Tick=0
 integer udg_A364Scene=0
 integer udg_A364Ability=0
 string udg_A364Forward=""
 string udg_A364Reverse=""
endglobals
function A364Mark takes string label returns nothing
 call Preload("A364 tick="+I2S(udg_A364Tick)+" scene="+I2S(udg_A364Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_A364Unit))+" order="+I2S(GetUnitCurrentOrder(udg_A364Unit))+" level="+I2S(GetUnitAbilityLevel(udg_A364Unit,udg_A364Ability)))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function A364Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_A364Tick,80)
 local boolean ok=false
 if age==0 then
  if udg_A364Scene==0 then
   set udg_A364Unit=CreateUnit(Player(0),'ospw',640,1024,0)
   set udg_A364Ability='Acpf'
   set udg_A364Forward="corporealform"
   set udg_A364Reverse="uncorporealform"
  elseif udg_A364Scene==1 then
   set udg_A364Unit=CreateUnit(Player(0),'ospm',640,1024,0)
   set udg_A364Ability='Aetf'
   set udg_A364Forward="etherealform"
   set udg_A364Reverse="unetherealform"
  else
   if udg_A364Scene==2 then
    set udg_A364Unit=CreateUnit(Player(0),'nmys',640,1024,0)
    set udg_A364Ability='Asb1'
   elseif udg_A364Scene==3 then
    set udg_A364Unit=CreateUnit(Player(0),'nnrs',640,1024,0)
    set udg_A364Ability='Asb2'
   else
    set udg_A364Unit=CreateUnit(Player(0),'nsbs',640,1024,0)
    set udg_A364Ability='Asb3'
   endif
   set udg_A364Forward="submerge"
   set udg_A364Reverse="unsubmerge"
  endif
  call A364Mark("before-attach")
  set ok=UnitAddAbility(udg_A364Unit,udg_A364Ability)
  if ok then
   call A364Mark("attach-accepted")
  else
   call A364Mark("attach-rejected")
  endif
  call UnitMakeAbilityPermanent(udg_A364Unit,true,udg_A364Ability)
  call SetUnitAcquireRange(udg_A364Unit,0)
  call SetUnitState(udg_A364Unit,UNIT_STATE_MANA,1000)
 elseif age==2 or age==22 or age==42 then
  if age==22 then
   set ok=IssueImmediateOrder(udg_A364Unit,udg_A364Forward)
  else
   set ok=IssueImmediateOrder(udg_A364Unit,udg_A364Reverse)
  endif
  if ok then
   call A364Mark("cast-accepted")
  else
   call A364Mark("cast-rejected")
  endif
 elseif age==62 then
  set ok=UnitRemoveAbility(udg_A364Unit,udg_A364Ability)
  if ok then
   call A364Mark("remove-accepted")
  else
   call A364Mark("remove-rejected")
  endif
 elseif age==77 then
  call RemoveUnit(udg_A364Unit)
  call A364Mark("cleanup")
 elseif age==79 then
  if udg_A364Scene==4 then
   call A364Mark("complete")
   call PauseTimer(udg_A364Timer)
  endif
  set udg_A364Scene=udg_A364Scene+1
 endif
 if age<77 then
  call A364Mark("sample")
 endif
 set udg_A364Tick=udg_A364Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetRandomSeed(1364)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call A364Mark("start")
 set udg_A364Timer=CreateTimer()
 call TimerStart(udg_A364Timer,0.1,true,function A364Tick)
endfunction
