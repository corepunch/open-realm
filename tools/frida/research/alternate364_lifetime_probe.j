globals
 timer udg_L364Timer=null
 unit udg_L364Unit=null
 integer udg_L364Tick=0
 integer udg_L364Scene=0
 integer udg_L364Ability=0
 string udg_L364Forward=""
 string udg_L364Reverse=""
endglobals
function L364Mark takes string label returns nothing
 call Preload("L364 tick="+I2S(udg_L364Tick)+" scene="+I2S(udg_L364Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_L364Unit))+" order="+I2S(GetUnitCurrentOrder(udg_L364Unit))+" level="+I2S(GetUnitAbilityLevel(udg_L364Unit,udg_L364Ability)))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function L364Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_L364Tick,80)
 local boolean ok=false
 if age==0 then
  if udg_L364Scene==0 then
   set udg_L364Unit=CreateUnit(Player(0),'ospw',640,1024,0)
   set udg_L364Ability='Acpf'
   set udg_L364Forward="corporealform"
   set udg_L364Reverse="uncorporealform"
  elseif udg_L364Scene==1 then
   set udg_L364Unit=CreateUnit(Player(0),'ospm',640,1024,0)
   set udg_L364Ability='Aetf'
   set udg_L364Forward="etherealform"
   set udg_L364Reverse="unetherealform"
  else
   if udg_L364Scene==2 then
    set udg_L364Unit=CreateUnit(Player(0),'nmys',640,1024,0)
    set udg_L364Ability='Asb1'
   elseif udg_L364Scene==3 then
    set udg_L364Unit=CreateUnit(Player(0),'nnrs',640,1024,0)
    set udg_L364Ability='Asb2'
   else
    set udg_L364Unit=CreateUnit(Player(0),'nsbs',640,1024,0)
    set udg_L364Ability='Asb3'
   endif
   set udg_L364Forward="submerge"
   set udg_L364Reverse="unsubmerge"
  endif
  call L364Mark("before-attach")
  set ok=UnitAddAbility(udg_L364Unit,udg_L364Ability)
  if ok then
   call L364Mark("attach-accepted")
  else
   call L364Mark("attach-rejected")
  endif
  call UnitMakeAbilityPermanent(udg_L364Unit,true,udg_L364Ability)
  call SetUnitAcquireRange(udg_L364Unit,0)
  call SetUnitState(udg_L364Unit,UNIT_STATE_MANA,1000)
 elseif age==22 or age==42 then
  if age==22 then
   set ok=IssueImmediateOrder(udg_L364Unit,udg_L364Forward)
  else
   set ok=IssueImmediateOrder(udg_L364Unit,udg_L364Reverse)
  endif
  if ok then
   call L364Mark("cast-accepted")
  else
   call L364Mark("cast-rejected")
  endif
 elseif age==62 then
  set ok=UnitRemoveAbility(udg_L364Unit,udg_L364Ability)
  if ok then
   call L364Mark("remove-accepted")
  else
   call L364Mark("remove-rejected")
  endif
 elseif age==77 then
  call RemoveUnit(udg_L364Unit)
  call L364Mark("cleanup")
 elseif age==79 then
  if udg_L364Scene==4 then
   call L364Mark("complete")
   call PauseTimer(udg_L364Timer)
  endif
  set udg_L364Scene=udg_L364Scene+1
 endif
 if age<77 then
  call L364Mark("sample")
 endif
 set udg_L364Tick=udg_L364Tick+1
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
 call L364Mark("start")
 set udg_L364Timer=CreateTimer()
 call TimerStart(udg_L364Timer,0.1,true,function L364Tick)
endfunction
