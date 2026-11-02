globals
 timer udg_P382VTimer=null
 unit udg_P382VUnit=null
 unit udg_P382VCaster=null
 unit udg_P382VPositiveCaster=null
 unit udg_P382VControl=null
 integer udg_P382VTick=0
 integer udg_P382VScene=0
 integer udg_P382VAbility='AEme'
 integer udg_P382VSetting=0
endglobals
function P382VMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 local integer immune=0
 if IsUnitType(udg_P382VUnit,UNIT_TYPE_MAGIC_IMMUNE) then
  set immune=1
 endif
 if IsUnitPaused(udg_P382VUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382VUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382V setting="+I2S(udg_P382VSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382VTick)+" scene="+I2S(udg_P382VScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382VUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382VUnit,udg_P382VAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382VUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382VUnit,'BEme'))+" immune="+I2S(immune)+" sleep="+I2S(GetUnitAbilityLevel(udg_P382VUnit,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(udg_P382VUnit,'BUsp'))+" controlSleep="+I2S(GetUnitAbilityLevel(udg_P382VControl,'BUsl'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382VUnit)))
 call PreloadGenEnd("payoff382v.txt")
endfunction
function P382VTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382VTick,160)
 local integer mode=0
 if age==0 then
  if mode==1 then
   set udg_P382VUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382VUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  set udg_P382VCaster=CreateUnit(Player(2),'Udre',900,1024,180)
  set udg_P382VPositiveCaster=CreateUnit(Player(2),'Udre',1350,1024,180)
  call SetUnitInvulnerable(udg_P382VPositiveCaster,true)
  call SetUnitAcquireRange(udg_P382VPositiveCaster,0)
  call UnitAddAbility(udg_P382VPositiveCaster,'AUsl')
  call SetUnitAbilityLevel(udg_P382VPositiveCaster,'AUsl',1)
  call SetUnitState(udg_P382VPositiveCaster,UNIT_STATE_MANA,1000)
  set udg_P382VControl=CreateUnit(Player(0),'Edem',1100,1024,0)
  call SetUnitInvulnerable(udg_P382VCaster,true)
  call UnitAddAbility(udg_P382VCaster,'Adt1')
  call SetUnitAcquireRange(udg_P382VCaster,0)
  call SetUnitAcquireRange(udg_P382VControl,0)
  call SetUnitAcquireRange(udg_P382VUnit,0)
  call SetUnitState(udg_P382VUnit,UNIT_STATE_MANA,1000)
  call P382VMark("created",true)
 elseif age==1 then
  call P382VMark("add-morph",UnitAddAbility(udg_P382VUnit,udg_P382VAbility))
  call UnitMakeAbilityPermanent(udg_P382VUnit,true,udg_P382VAbility)
  call P382VMark("add-sleep-producer",UnitAddAbility(udg_P382VCaster,'AUsl'))
  call SetUnitAbilityLevel(udg_P382VCaster,'AUsl',1)
  call SetUnitState(udg_P382VCaster,UNIT_STATE_MANA,1000)
 elseif age==2 then
  call P382VMark("add-passive-before",UnitAddAbility(udg_P382VUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382VUnit,true,'Apiv')
 elseif age==5 then
  call P382VMark("independent-sleep-positive",IssueTargetOrder(udg_P382VPositiveCaster,"sleep",udg_P382VControl))
 elseif age==10 then
  call P382VMark("independent-sleep-witness",true)
 elseif age==11 and mode==0 then
  call P382VMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382VMark("forward",IssueImmediateOrder(udg_P382VUnit,"metamorphosis"))
 elseif age==18 then
  call P382VMark("remove-positive-sleep",UnitRemoveAbility(udg_P382VControl,'BUsl'))
 elseif age==20 then
  call P382VMark("sleep-morphed-actor",IssueTargetOrder(udg_P382VCaster,"sleep",udg_P382VUnit))
 elseif age==22 and udg_P382VScene<3 then
  call PauseUnit(udg_P382VUnit,true)
  call P382VMark("pause",true)
 elseif age==23 and udg_P382VScene==2 then
  call P382VMark("remove-sleep-while-paused",UnitRemoveAbility(udg_P382VUnit,'BUsl'))
 elseif age==50 then
  call P382VMark("held-across-natural-lifetime",true)
 elseif age==55 and udg_P382VScene<3 then
  call PauseUnit(udg_P382VUnit,false)
  call P382VMark("unpause",true)
 elseif age==60 and udg_P382VScene==1 then
  call P382VMark("remove-sleep-after-unpause",UnitRemoveAbility(udg_P382VUnit,'BUsl'))
 elseif age==145 then
  call P382VMark("post-resume-visibility",true)
 elseif age==153 then
  call P382VMark("remove-passive",UnitRemoveAbility(udg_P382VUnit,'Apiv'))
 elseif age==155 then
  call RemoveUnit(udg_P382VPositiveCaster)
  call RemoveUnit(udg_P382VCaster)
  call RemoveUnit(udg_P382VControl)
  call RemoveUnit(udg_P382VUnit)
  call P382VMark("cleanup",true)
 elseif age==159 then
  if udg_P382VScene==3 then
   call P382VMark("complete",true)
   call PauseTimer(udg_P382VTimer)
  endif
  set udg_P382VScene=udg_P382VScene+1
 endif
 if age<155 then
  call P382VMark("sample",true)
 endif
 set udg_P382VTick=udg_P382VTick+1
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
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 call Preload("P382V tick=0 label=start")
 set udg_P382VTimer=CreateTimer()
 call TimerStart(udg_P382VTimer,0.1,true,function P382VTick)
endfunction
