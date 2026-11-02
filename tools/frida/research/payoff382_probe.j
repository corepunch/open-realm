globals
 timer udg_P382Timer=null
 unit udg_P382Unit=null
 integer udg_P382Tick=0
 integer udg_P382Scene=0
 integer udg_P382Ability='AEme'
 integer udg_P382Setting=0
endglobals
function P382Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382 setting="+I2S(udg_P382Setting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382Tick)+" scene="+I2S(udg_P382Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382Unit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382Unit,udg_P382Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_P382Unit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382Unit,'BEme'))+" order="+I2S(GetUnitCurrentOrder(udg_P382Unit)))
 call PreloadGenEnd("payoff382.txt")
endfunction
function P382Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382Tick,70)
 local integer mode=ModuloInteger(udg_P382Scene,3)
 if age==0 then
  if mode==2 then
   set udg_P382Unit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382Unit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P382Unit,0)
  call SetUnitState(udg_P382Unit,UNIT_STATE_MANA,1000)
  call P382Mark("created",true)
 elseif age==1 then
  call P382Mark("add-morph",UnitAddAbility(udg_P382Unit,udg_P382Ability))
  call UnitMakeAbilityPermanent(udg_P382Unit,true,udg_P382Ability)
 elseif age==2 and mode==0 then
  call P382Mark("add-passive-before",UnitAddAbility(udg_P382Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382Unit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P382Mark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P382Mark("forward",IssueImmediateOrder(udg_P382Unit,"metamorphosis"))
 elseif age==22 and mode!=0 then
  call P382Mark("add-passive-after",UnitAddAbility(udg_P382Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382Unit,true,'Apiv')
 elseif age==35 then
  call P382Mark("natural-lifetime-sample",true)
 elseif age==50 then
  call P382Mark("post-reverse-visibility",true)
 elseif age==60 then
  call P382Mark("remove-passive",UnitRemoveAbility(udg_P382Unit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P382Unit)
  call P382Mark("cleanup",true)
 elseif age==69 then
  if udg_P382Scene==2 then
   call P382Mark("complete",true)
   call PauseTimer(udg_P382Timer)
  endif
  set udg_P382Scene=udg_P382Scene+1
 endif
 if age<65 then
  call P382Mark("sample",true)
 endif
 set udg_P382Tick=udg_P382Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1382)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_CONTROL,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_CONTROL,false)
 call Preload("P382 tick=0 label=start")
 set udg_P382Timer=CreateTimer()
 call TimerStart(udg_P382Timer,0.1,true,function P382Tick)
endfunction
