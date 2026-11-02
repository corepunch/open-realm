globals
 timer udg_P382DTimer=null
 unit udg_P382DUnit=null
 integer udg_P382DTick=0
 integer udg_P382DScene=0
 integer udg_P382DAbility='AEme'
 integer udg_P382DSetting=0
endglobals
function P382DMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382DUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382D setting="+I2S(udg_P382DSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382DTick)+" scene="+I2S(udg_P382DScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382DUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382DUnit,udg_P382DAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382DUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382DUnit,'BEme'))+" order="+I2S(GetUnitCurrentOrder(udg_P382DUnit)))
 call PreloadGenEnd("payoff382d.txt")
endfunction
function P382DTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382DTick,70)
 local integer mode=ModuloInteger(udg_P382DScene,2)
 if age==0 then
  set udg_P382DUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  call SetUnitAcquireRange(udg_P382DUnit,0)
  call SetUnitState(udg_P382DUnit,UNIT_STATE_MANA,1000)
  call P382DMark("created",true)
 elseif age==1 then
  call P382DMark("add-morph",UnitAddAbility(udg_P382DUnit,udg_P382DAbility))
  call UnitMakeAbilityPermanent(udg_P382DUnit,true,udg_P382DAbility)
 elseif age==2 then
  call P382DMark("add-passive-before",UnitAddAbility(udg_P382DUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382DUnit,true,'Apiv')
 elseif age==11 then
  call P382DMark("pre-forward-visibility",true)
 elseif age==12 then
  call P382DMark("forward",IssueImmediateOrder(udg_P382DUnit,"metamorphosis"))
 elseif (age==13 and mode==0) or (age==22 and mode==1) then
  call P382DMark("detach-morph",UnitRemoveAbility(udg_P382DUnit,udg_P382DAbility))
 elseif age==35 then
  call P382DMark("after-detach-fade",true)
 elseif age==50 then
  call P382DMark("later-detach-visibility",true)
 elseif age==60 then
  call P382DMark("remove-passive",UnitRemoveAbility(udg_P382DUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P382DUnit)
  call P382DMark("cleanup",true)
 elseif age==69 then
  if udg_P382DScene==1 then
   call P382DMark("complete",true)
   call PauseTimer(udg_P382DTimer)
  endif
  set udg_P382DScene=udg_P382DScene+1
 endif
 if age<65 then
  call P382DMark("sample",true)
 endif
 set udg_P382DTick=udg_P382DTick+1
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
 call Preload("P382D tick=0 label=start")
 set udg_P382DTimer=CreateTimer()
 call TimerStart(udg_P382DTimer,0.1,true,function P382DTick)
endfunction
