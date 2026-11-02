globals
 timer udg_P380DTimer=null
 unit udg_P380DUnit=null
 integer udg_P380DTick=0
 integer udg_P380DScene=0
 integer udg_P380DAbility='ANcr'
 integer udg_P380DSetting=0
endglobals
function P380DMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P380DUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P380D setting="+I2S(udg_P380DSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P380DTick)+" scene="+I2S(udg_P380DScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P380DUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P380DUnit,udg_P380DAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P380DUnit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P380DUnit)))
 call PreloadGenEnd("payoff380d.txt")
endfunction
function P380DTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P380DTick,70)
 local integer mode=ModuloInteger(udg_P380DScene,2)
 if age==0 then
  set udg_P380DUnit=CreateUnit(Player(0),'Nalc',640,1024,0)
  call SetUnitAcquireRange(udg_P380DUnit,0)
  call SetUnitState(udg_P380DUnit,UNIT_STATE_MANA,1000)
  call P380DMark("created",true)
 elseif age==1 then
  call P380DMark("add-morph",UnitAddAbility(udg_P380DUnit,udg_P380DAbility))
  call UnitMakeAbilityPermanent(udg_P380DUnit,true,udg_P380DAbility)
 elseif age==2 then
  call P380DMark("add-passive-before",UnitAddAbility(udg_P380DUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P380DUnit,true,'Apiv')
 elseif age==11 then
  call P380DMark("pre-forward-visibility",true)
 elseif age==12 then
  call P380DMark("forward",IssueImmediateOrder(udg_P380DUnit,"chemicalrage"))
 elseif (age==13 and mode==0) or (age==22 and mode==1) then
  call P380DMark("detach-morph",UnitRemoveAbility(udg_P380DUnit,udg_P380DAbility))
 elseif age==35 then
  call P380DMark("after-detach-fade",true)
 elseif age==50 then
  call P380DMark("later-detach-visibility",true)
 elseif age==60 then
  call P380DMark("remove-passive",UnitRemoveAbility(udg_P380DUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P380DUnit)
  call P380DMark("cleanup",true)
 elseif age==69 then
  if udg_P380DScene==1 then
   call P380DMark("complete",true)
   call PauseTimer(udg_P380DTimer)
  endif
  set udg_P380DScene=udg_P380DScene+1
 endif
 if age<65 then
  call P380DMark("sample",true)
 endif
 set udg_P380DTick=udg_P380DTick+1
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
 call Preload("P380D tick=0 label=start")
 set udg_P380DTimer=CreateTimer()
 call TimerStart(udg_P380DTimer,0.1,true,function P380DTick)
endfunction
