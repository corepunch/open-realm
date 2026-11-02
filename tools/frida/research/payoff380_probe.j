globals
 timer udg_P380Timer=null
 unit udg_P380Unit=null
 integer udg_P380Tick=0
 integer udg_P380Scene=0
 integer udg_P380Ability='ANcr'
 integer udg_P380Setting=0
endglobals
function P380Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P380Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("P380 setting="+I2S(udg_P380Setting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P380Tick)+" scene="+I2S(udg_P380Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P380Unit))+" morph="+I2S(GetUnitAbilityLevel(udg_P380Unit,udg_P380Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_P380Unit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P380Unit)))
 call PreloadGenEnd("payoff380.txt")
endfunction
function P380Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P380Tick,70)
 local integer mode=ModuloInteger(udg_P380Scene,3)
 if age==0 then
  if mode==2 then
   set udg_P380Unit=CreateUnit(Player(0),'Nalm',640,1024,0)
  else
   set udg_P380Unit=CreateUnit(Player(0),'Nalc',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P380Unit,0)
  call SetUnitState(udg_P380Unit,UNIT_STATE_MANA,1000)
  call P380Mark("created",true)
 elseif age==1 then
  call P380Mark("add-morph",UnitAddAbility(udg_P380Unit,udg_P380Ability))
  call UnitMakeAbilityPermanent(udg_P380Unit,true,udg_P380Ability)
 elseif age==2 and mode==0 then
  call P380Mark("add-passive-before",UnitAddAbility(udg_P380Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P380Unit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P380Mark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P380Mark("forward",IssueImmediateOrder(udg_P380Unit,"chemicalrage"))
 elseif age==22 and mode!=0 then
  call P380Mark("add-passive-after",UnitAddAbility(udg_P380Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P380Unit,true,'Apiv')
 elseif age==35 then
  call P380Mark("natural-lifetime-sample",true)
 elseif age==50 then
  call P380Mark("post-reverse-visibility",true)
 elseif age==60 then
  call P380Mark("remove-passive",UnitRemoveAbility(udg_P380Unit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P380Unit)
  call P380Mark("cleanup",true)
 elseif age==69 then
  if udg_P380Scene==2 then
   call P380Mark("complete",true)
   call PauseTimer(udg_P380Timer)
  endif
  set udg_P380Scene=udg_P380Scene+1
 endif
 if age<65 then
  call P380Mark("sample",true)
 endif
 set udg_P380Tick=udg_P380Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1380)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_CONTROL,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_CONTROL,false)
 call Preload("P380 tick=0 label=start")
 set udg_P380Timer=CreateTimer()
 call TimerStart(udg_P380Timer,0.1,true,function P380Tick)
endfunction
