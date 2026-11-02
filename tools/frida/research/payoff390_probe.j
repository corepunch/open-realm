globals
 timer udg_P390Timer=null
 unit udg_P390Unit=null
 integer udg_P390Tick=0
 integer udg_P390Scene=0
 integer udg_P390Ability='Astn'
 integer udg_P390Setting=0
endglobals
function P390Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P390Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("P390 setting="+I2S(udg_P390Setting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P390Tick)+" scene="+I2S(udg_P390Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P390Unit))+" morph="+I2S(GetUnitAbilityLevel(udg_P390Unit,udg_P390Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_P390Unit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P390Unit)))
 call PreloadGenEnd("payoff390.txt")
endfunction
function P390Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P390Tick,70)
 local integer mode=ModuloInteger(udg_P390Scene,3)
 if age==0 then
  if mode==2 then
   set udg_P390Unit=CreateUnit(Player(0),'ugrm',640,1024,0)
  else
   set udg_P390Unit=CreateUnit(Player(0),'ugar',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P390Unit,0)
  call SetUnitState(udg_P390Unit,UNIT_STATE_MANA,1000)
  call P390Mark("created",true)
 elseif age==1 then
  call P390Mark("add-morph",UnitAddAbility(udg_P390Unit,udg_P390Ability))
  call UnitMakeAbilityPermanent(udg_P390Unit,true,udg_P390Ability)
 elseif age==2 and mode==0 then
  call P390Mark("add-passive-before",UnitAddAbility(udg_P390Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P390Unit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P390Mark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P390Mark("forward",IssueImmediateOrder(udg_P390Unit,"stoneform"))
 elseif age==25 and mode!=0 then
  call P390Mark("add-passive-after",UnitAddAbility(udg_P390Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P390Unit,true,'Apiv')
 elseif age==35 then
  call P390Mark("reverse",IssueImmediateOrder(udg_P390Unit,"unstoneform"))
 elseif age==50 then
  call P390Mark("post-reverse-visibility",true)
 elseif age==60 then
  call P390Mark("remove-passive",UnitRemoveAbility(udg_P390Unit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P390Unit)
  call P390Mark("cleanup",true)
 elseif age==69 then
  if udg_P390Scene==2 then
   call P390Mark("complete",true)
   call PauseTimer(udg_P390Timer)
  endif
  set udg_P390Scene=udg_P390Scene+1
 endif
 if age<65 then
  call P390Mark("sample",true)
 endif
 set udg_P390Tick=udg_P390Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1390)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_CONTROL,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_CONTROL,false)
 call Preload("P390 tick=0 label=start")
 set udg_P390Timer=CreateTimer()
 call TimerStart(udg_P390Timer,0.1,true,function P390Tick)
endfunction
