globals
 timer udg_P390DTimer=null
 unit udg_P390DUnit=null
 integer udg_P390DTick=0
 integer udg_P390DScene=0
 integer udg_P390DAbility='Astn'
 integer udg_P390DSetting=0
endglobals
function P390DMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P390DUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P390D setting="+I2S(udg_P390DSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P390DTick)+" scene="+I2S(udg_P390DScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P390DUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P390DUnit,udg_P390DAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P390DUnit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P390DUnit)))
 call PreloadGenEnd("payoff390d.txt")
endfunction
function P390DTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P390DTick,70)
 local integer mode=ModuloInteger(udg_P390DScene,2)
 if age==0 then
  set udg_P390DUnit=CreateUnit(Player(0),'ugar',640,1024,0)
  call SetUnitAcquireRange(udg_P390DUnit,0)
  call SetUnitState(udg_P390DUnit,UNIT_STATE_MANA,1000)
  call P390DMark("created",true)
 elseif age==1 then
  call P390DMark("add-morph",UnitAddAbility(udg_P390DUnit,udg_P390DAbility))
  call UnitMakeAbilityPermanent(udg_P390DUnit,true,udg_P390DAbility)
 elseif age==2 then
  call P390DMark("add-passive-before",UnitAddAbility(udg_P390DUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P390DUnit,true,'Apiv')
 elseif age==11 then
  call P390DMark("pre-forward-visibility",true)
 elseif age==12 then
  call P390DMark("forward",IssueImmediateOrder(udg_P390DUnit,"stoneform"))
 elseif (age==13 and mode==0) or (age==25 and mode==1) then
  call P390DMark("detach-morph",UnitRemoveAbility(udg_P390DUnit,udg_P390DAbility))
 elseif age==35 then
  call P390DMark("after-detach-fade",true)
 elseif age==50 then
  call P390DMark("later-detach-visibility",true)
 elseif age==60 then
  call P390DMark("remove-passive",UnitRemoveAbility(udg_P390DUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P390DUnit)
  call P390DMark("cleanup",true)
 elseif age==69 then
  if udg_P390DScene==1 then
   call P390DMark("complete",true)
   call PauseTimer(udg_P390DTimer)
  endif
  set udg_P390DScene=udg_P390DScene+1
 endif
 if age<65 then
  call P390DMark("sample",true)
 endif
 set udg_P390DTick=udg_P390DTick+1
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
 call Preload("P390D tick=0 label=start")
 set udg_P390DTimer=CreateTimer()
 call TimerStart(udg_P390DTimer,0.1,true,function P390DTick)
endfunction
