globals
 timer udg_P375DTimer=null
 unit udg_P375DUnit=null
 integer udg_P375DTick=0
 integer udg_P375DScene=0
 integer udg_P375DAbility='ANrg'
 integer udg_P375DSetting=0
endglobals
function P375DMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P375DUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P375D setting="+I2S(udg_P375DSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P375DTick)+" scene="+I2S(udg_P375DScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P375DUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P375DUnit,udg_P375DAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P375DUnit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_P375DUnit)))
 call PreloadGenEnd("payoff375d.txt")
endfunction
function P375DTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P375DTick,70)
 local integer mode=ModuloInteger(udg_P375DScene,2)
 if age==0 then
  set udg_P375DUnit=CreateUnit(Player(0),'Ntin',640,1024,0)
  call SetUnitAcquireRange(udg_P375DUnit,0)
  call SetUnitState(udg_P375DUnit,UNIT_STATE_MANA,1000)
  call P375DMark("created",true)
 elseif age==1 then
  call P375DMark("add-morph",UnitAddAbility(udg_P375DUnit,udg_P375DAbility))
  call UnitMakeAbilityPermanent(udg_P375DUnit,true,udg_P375DAbility)
 elseif age==2 then
  call P375DMark("add-passive-before",UnitAddAbility(udg_P375DUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P375DUnit,true,'Apiv')
 elseif age==11 then
  call P375DMark("pre-forward-visibility",true)
 elseif age==12 then
  call P375DMark("forward",IssueImmediateOrder(udg_P375DUnit,"robogoblin"))
 elseif (age==13 and mode==0) or (age==22 and mode==1) then
  call P375DMark("detach-morph",UnitRemoveAbility(udg_P375DUnit,udg_P375DAbility))
 elseif age==35 then
  call P375DMark("after-detach-fade",true)
 elseif age==50 then
  call P375DMark("later-detach-visibility",true)
 elseif age==60 then
  call P375DMark("remove-passive",UnitRemoveAbility(udg_P375DUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P375DUnit)
  call P375DMark("cleanup",true)
 elseif age==69 then
  if udg_P375DScene==1 then
   call P375DMark("complete",true)
   call PauseTimer(udg_P375DTimer)
  endif
  set udg_P375DScene=udg_P375DScene+1
 endif
 if age<65 then
  call P375DMark("sample",true)
 endif
 set udg_P375DTick=udg_P375DTick+1
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
 call Preload("P375D tick=0 label=start")
 set udg_P375DTimer=CreateTimer()
 call TimerStart(udg_P375DTimer,0.1,true,function P375DTick)
endfunction
