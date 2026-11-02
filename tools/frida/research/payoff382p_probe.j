globals
 timer udg_P382PTimer=null
 unit udg_P382PUnit=null
 integer udg_P382PTick=0
 integer udg_P382PScene=0
 integer udg_P382PAbility='AEme'
 integer udg_P382PSetting=0
endglobals
function P382PMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 if IsUnitPaused(udg_P382PUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382PUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382P setting="+I2S(udg_P382PSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382PTick)+" scene="+I2S(udg_P382PScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382PUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382PUnit,udg_P382PAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382PUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382PUnit,'BEme'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382PUnit)))
 call PreloadGenEnd("payoff382p.txt")
endfunction
function P382PTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382PTick,90)
 local integer mode=ModuloInteger(udg_P382PScene,2)
 if age==0 then
  if mode==1 then
   set udg_P382PUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382PUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P382PUnit,0)
  call SetUnitState(udg_P382PUnit,UNIT_STATE_MANA,1000)
  call P382PMark("created",true)
 elseif age==1 then
  call P382PMark("add-morph",UnitAddAbility(udg_P382PUnit,udg_P382PAbility))
  call UnitMakeAbilityPermanent(udg_P382PUnit,true,udg_P382PAbility)
 elseif age==2 then
  call P382PMark("add-passive-before",UnitAddAbility(udg_P382PUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382PUnit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P382PMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382PMark("forward",IssueImmediateOrder(udg_P382PUnit,"metamorphosis"))
 elseif age==22 then
  call PauseUnit(udg_P382PUnit,true)
  call P382PMark("pause",true)
 elseif age==23 and udg_P382PScene>=2 then
  call P382PMark("remove-metamorphosis-buff",UnitRemoveAbility(udg_P382PUnit,'BEme'))
 elseif age==50 then
  call P382PMark("held-across-natural-lifetime",true)
 elseif age==55 then
  call PauseUnit(udg_P382PUnit,false)
  call P382PMark("unpause",true)
 elseif age==80 then
  call P382PMark("post-resume-visibility",true)
 elseif age==83 then
  call P382PMark("remove-passive",UnitRemoveAbility(udg_P382PUnit,'Apiv'))
 elseif age==85 then
  call RemoveUnit(udg_P382PUnit)
  call P382PMark("cleanup",true)
 elseif age==89 then
  if udg_P382PScene==3 then
   call P382PMark("complete",true)
   call PauseTimer(udg_P382PTimer)
  endif
  set udg_P382PScene=udg_P382PScene+1
 endif
 if age<85 then
  call P382PMark("sample",true)
 endif
 set udg_P382PTick=udg_P382PTick+1
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
 call Preload("P382P tick=0 label=start")
 set udg_P382PTimer=CreateTimer()
 call TimerStart(udg_P382PTimer,0.1,true,function P382PTick)
endfunction
