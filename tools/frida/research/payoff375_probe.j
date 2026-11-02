globals
 timer udg_P375Timer=null
 unit udg_P375Unit=null
 integer udg_P375Tick=0
 integer udg_P375Scene=0
 integer udg_P375Ability='ANrg'
 integer udg_P375Setting=0
endglobals
function P375Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P375Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("P375 setting="+I2S(udg_P375Setting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P375Tick)+" scene="+I2S(udg_P375Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P375Unit))+" morph="+I2S(GetUnitAbilityLevel(udg_P375Unit,udg_P375Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_P375Unit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P375Unit)))
 call PreloadGenEnd("payoff375.txt")
endfunction
function P375Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P375Tick,70)
 local integer mode=ModuloInteger(udg_P375Scene,3)
 if age==0 then
  if mode==2 then
   set udg_P375Unit=CreateUnit(Player(0),'Nrob',640,1024,0)
  else
   set udg_P375Unit=CreateUnit(Player(0),'Ntin',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P375Unit,0)
  call SetUnitState(udg_P375Unit,UNIT_STATE_MANA,1000)
  call P375Mark("created",true)
 elseif age==1 then
  call P375Mark("add-morph",UnitAddAbility(udg_P375Unit,udg_P375Ability))
  call UnitMakeAbilityPermanent(udg_P375Unit,true,udg_P375Ability)
 elseif age==2 and mode==0 then
  call P375Mark("add-passive-before",UnitAddAbility(udg_P375Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P375Unit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P375Mark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P375Mark("forward",IssueImmediateOrder(udg_P375Unit,"robogoblin"))
 elseif age==22 and mode!=0 then
  call P375Mark("add-passive-after",UnitAddAbility(udg_P375Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P375Unit,true,'Apiv')
 elseif age==35 then
  call P375Mark("reverse",IssueImmediateOrder(udg_P375Unit,"unrobogoblin"))
 elseif age==50 then
  call P375Mark("post-reverse-visibility",true)
 elseif age==60 then
  call P375Mark("remove-passive",UnitRemoveAbility(udg_P375Unit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P375Unit)
  call P375Mark("cleanup",true)
 elseif age==69 then
  if udg_P375Scene==2 then
   call P375Mark("complete",true)
   call PauseTimer(udg_P375Timer)
  endif
  set udg_P375Scene=udg_P375Scene+1
 endif
 if age<65 then
  call P375Mark("sample",true)
 endif
 set udg_P375Tick=udg_P375Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1375)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_CONTROL,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_CONTROL,false)
 call Preload("P375 tick=0 label=start")
 set udg_P375Timer=CreateTimer()
 call TimerStart(udg_P375Timer,0.1,true,function P375Tick)
endfunction
