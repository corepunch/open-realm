globals
 timer udg_M334Timer=null
 unit udg_M334Unit=null
 integer udg_M334Tick=0
 integer udg_M334Scene=0
endglobals
function M334Mark takes string label returns nothing
 call Preload("M334 tick="+I2S(udg_M334Tick)+" scene="+I2S(udg_M334Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M334Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M334Unit)))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function M334Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M334Tick,40)
 local integer form=udg_M334Scene/3
 local integer action=ModuloInteger(udg_M334Scene,3)
 local integer abilityId='Astn'
 local boolean ok=false
 if form==1 then
  set abilityId='Aave'
 endif
 if age==0 then
  if form==0 then
   set udg_M334Unit=CreateUnit(Player(0),'ugar',640,1024,0)
  else
   set udg_M334Unit=CreateUnit(Player(0),'uobs',640,1024,0)
  endif
  call UnitAddAbility(udg_M334Unit,abilityId)
  call SetUnitAcquireRange(udg_M334Unit,0)
  call SetUnitState(udg_M334Unit,UNIT_STATE_MANA,1000)
  call M334Mark("created")
 elseif age==2 then
  if form==0 then
   set ok=IssueImmediateOrder(udg_M334Unit,"stoneform")
  else
   set ok=IssueImmediateOrder(udg_M334Unit,"avengerform")
  endif
  if ok then
   call M334Mark("cast-accepted")
  else
   call M334Mark("cast-rejected")
  endif
 elseif (age==3 and action==0) or (age==20 and action==1) then
  set ok=UnitRemoveAbility(udg_M334Unit,abilityId)
  if ok then
   call M334Mark("remove-accepted")
  else
   call M334Mark("remove-rejected")
  endif
 elseif age==3 and action==2 then
  call KillUnit(udg_M334Unit)
  call M334Mark("kill")
 elseif age==37 then
  call RemoveUnit(udg_M334Unit)
  call M334Mark("cleanup")
 elseif age==39 then
  if udg_M334Scene==5 then
   call M334Mark("complete")
   call PauseTimer(udg_M334Timer)
  endif
  set udg_M334Scene=udg_M334Scene+1
 endif
 if age<37 then
  call M334Mark("sample")
 endif
 set udg_M334Tick=udg_M334Tick+1
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
 call SetRandomSeed(1334)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call M334Mark("start")
 set udg_M334Timer=CreateTimer()
 call TimerStart(udg_M334Timer,0.1,true,function M334Tick)
endfunction
