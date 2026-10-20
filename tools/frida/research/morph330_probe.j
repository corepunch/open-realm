globals
 timer udg_M330Timer=null
 unit udg_M330Unit=null
 integer udg_M330Tick=0
 integer udg_M330Scene=0
endglobals
function M330Mark takes string label returns nothing
 call Preload("M330 tick="+I2S(udg_M330Tick)+" scene="+I2S(udg_M330Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_M330Unit))+" order="+I2S(GetUnitCurrentOrder(udg_M330Unit)))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function M330Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M330Tick,90)
 local boolean ok=false
 if age==0 then
  if udg_M330Scene==0 then
   set udg_M330Unit=CreateUnit(Player(0),'Edem',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'AEme')
   call SetUnitAbilityLevel(udg_M330Unit,'AEme',1)
  elseif udg_M330Scene==1 then
   set udg_M330Unit=CreateUnit(Player(0),'Nalc',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'ANcr')
   call SetUnitAbilityLevel(udg_M330Unit,'ANcr',1)
  elseif udg_M330Scene==2 then
   set udg_M330Unit=CreateUnit(Player(0),'Ntin',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'ANrg')
   call SetUnitAbilityLevel(udg_M330Unit,'ANrg',1)
  elseif udg_M330Scene==3 then
   set udg_M330Unit=CreateUnit(Player(0),'uobs',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'Aave')
   call SetUnitAbilityLevel(udg_M330Unit,'Aave',1)
  elseif udg_M330Scene==4 then
   set udg_M330Unit=CreateUnit(Player(0),'ugar',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'Astn')
   call SetUnitAbilityLevel(udg_M330Unit,'Astn',1)
  elseif udg_M330Scene==5 then
   set udg_M330Unit=CreateUnit(Player(0),'ucry',640,1024,0)
   call UnitAddAbility(udg_M330Unit,'Abur')
   call SetUnitAbilityLevel(udg_M330Unit,'Abur',1)
  elseif udg_M330Scene==6 then
   set udg_M330Unit=CreateUnit(Player(0),'ospm',640,1024,0)
   call UnitRemoveAbility(udg_M330Unit,'Aetf')
   call UnitAddAbility(udg_M330Unit,'Acpf')
   call SetUnitAbilityLevel(udg_M330Unit,'Acpf',1)
  elseif udg_M330Scene==7 then
   set udg_M330Unit=CreateUnit(Player(0),'ospw',640,1024,0)
   call UnitRemoveAbility(udg_M330Unit,'Acpf')
   call UnitAddAbility(udg_M330Unit,'Aetf')
   call SetUnitAbilityLevel(udg_M330Unit,'Aetf',1)
  endif
  call SetUnitAcquireRange(udg_M330Unit,0)
  call SetUnitState(udg_M330Unit,UNIT_STATE_MANA,1000)
  call M330Mark("created")
 elseif age==2 then
  if udg_M330Scene==0 then
   set ok=IssueImmediateOrder(udg_M330Unit,"metamorphosis")
  elseif udg_M330Scene==1 then
   set ok=IssueImmediateOrder(udg_M330Unit,"chemicalrage")
  elseif udg_M330Scene==2 then
   set ok=IssueImmediateOrder(udg_M330Unit,"robogoblin")
  elseif udg_M330Scene==3 then
   set ok=IssueImmediateOrder(udg_M330Unit,"avengerform")
  elseif udg_M330Scene==4 then
   set ok=IssueImmediateOrder(udg_M330Unit,"stoneform")
  elseif udg_M330Scene==5 then
   set ok=IssueImmediateOrder(udg_M330Unit,"burrow")
  elseif udg_M330Scene==6 then
   set ok=IssueImmediateOrder(udg_M330Unit,"corporealform")
  elseif udg_M330Scene==7 then
   set ok=IssueImmediateOrder(udg_M330Unit,"etherealform")
  endif
  if ok then
   call M330Mark("cast-accepted")
  else
   call M330Mark("cast-rejected")
  endif
 elseif age==50 then
  if udg_M330Scene==2 then
   set ok=IssueImmediateOrder(udg_M330Unit,"unrobogoblin")
  elseif udg_M330Scene==4 then
   set ok=IssueImmediateOrder(udg_M330Unit,"unstoneform")
  elseif udg_M330Scene==5 then
   set ok=IssueImmediateOrder(udg_M330Unit,"unburrow")
  elseif udg_M330Scene==6 then
   set ok=IssueImmediateOrder(udg_M330Unit,"uncorporealform")
  elseif udg_M330Scene==7 then
   set ok=IssueImmediateOrder(udg_M330Unit,"unetherealform")
  endif
  if ok then
   call M330Mark("reverse-accepted")
  else
   call M330Mark("reverse-rejected-or-absent")
  endif
 elseif age==87 then
  call RemoveUnit(udg_M330Unit)
  call M330Mark("cleanup")
 elseif age==89 then
  if udg_M330Scene==7 then
   call M330Mark("complete")
   call PauseTimer(udg_M330Timer)
  endif
  set udg_M330Scene=udg_M330Scene+1
 endif
 if age<87 then
  call M330Mark("sample")
 endif
 set udg_M330Tick=udg_M330Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetRandomSeed(1330)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Rusp',1)
 call SetPlayerTechResearched(Player(0),'Ruba',1)
 call SetPlayerTechResearched(Player(0),'Rugf',1)
 call SetPlayerTechResearched(Player(0),'Rudf',1)
 call M330Mark("start")
 set udg_M330Timer=CreateTimer()
 call TimerStart(udg_M330Timer,0.1,true,function M330Tick)
endfunction
