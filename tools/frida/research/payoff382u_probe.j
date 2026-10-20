globals
 timer udg_P382UTimer=null
 unit udg_P382UUnit=null
 unit udg_P382UCaster=null
 unit udg_P382UControl=null
 integer udg_P382UTick=0
 integer udg_P382UScene=0
 integer udg_P382UAbility='AEme'
 integer udg_P382USetting=0
endglobals
function P382UMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 local integer immune=0
 if IsUnitType(udg_P382UUnit,UNIT_TYPE_MAGIC_IMMUNE) then
  set immune=1
 endif
 if IsUnitPaused(udg_P382UUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382UUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382U setting="+I2S(udg_P382USetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382UTick)+" scene="+I2S(udg_P382UScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382UUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382UUnit,udg_P382UAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382UUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382UUnit,'BEme'))+" immune="+I2S(immune)+" sleep="+I2S(GetUnitAbilityLevel(udg_P382UUnit,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(udg_P382UUnit,'BUsp'))+" controlSleep="+I2S(GetUnitAbilityLevel(udg_P382UControl,'BUsl'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382UUnit)))
 call PreloadGenEnd("payoff382u.txt")
endfunction
function P382UTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382UTick,160)
 local integer mode=0
 if age==0 then
  if mode==1 then
   set udg_P382UUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382UUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  set udg_P382UCaster=CreateUnit(Player(2),'Udre',900,1024,180)
  set udg_P382UControl=CreateUnit(Player(0),'Edem',1100,1024,0)
  call SetUnitInvulnerable(udg_P382UCaster,true)
  call UnitAddAbility(udg_P382UCaster,'Adt1')
  call SetUnitAcquireRange(udg_P382UCaster,0)
  call SetUnitAcquireRange(udg_P382UControl,0)
  call SetUnitAcquireRange(udg_P382UUnit,0)
  call SetUnitState(udg_P382UUnit,UNIT_STATE_MANA,1000)
  call P382UMark("created",true)
 elseif age==1 then
  call P382UMark("add-morph",UnitAddAbility(udg_P382UUnit,udg_P382UAbility))
  call UnitMakeAbilityPermanent(udg_P382UUnit,true,udg_P382UAbility)
  call P382UMark("add-sleep-producer",UnitAddAbility(udg_P382UCaster,'AUsl'))
  call SetUnitState(udg_P382UCaster,UNIT_STATE_MANA,1000)
 elseif age==2 then
  call P382UMark("add-passive-before",UnitAddAbility(udg_P382UUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382UUnit,true,'Apiv')
 elseif age==5 then
  call P382UMark("independent-sleep-positive",IssueTargetOrder(udg_P382UCaster,"sleep",udg_P382UControl))
 elseif age==10 then
  call P382UMark("independent-sleep-witness",true)
 elseif age==11 and mode==0 then
  call P382UMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382UMark("forward",IssueImmediateOrder(udg_P382UUnit,"metamorphosis"))
 elseif age==18 then
  call P382UMark("remove-positive-sleep",UnitRemoveAbility(udg_P382UControl,'BUsl'))
 elseif age==20 then
  call P382UMark("sleep-morphed-actor",IssueTargetOrder(udg_P382UCaster,"sleep",udg_P382UUnit))
 elseif age==22 and udg_P382UScene<3 then
  call PauseUnit(udg_P382UUnit,true)
  call P382UMark("pause",true)
 elseif age==23 and udg_P382UScene==2 then
  call P382UMark("remove-sleep-while-paused",UnitRemoveAbility(udg_P382UUnit,'BUsl'))
 elseif age==50 then
  call P382UMark("held-across-natural-lifetime",true)
 elseif age==55 and udg_P382UScene<3 then
  call PauseUnit(udg_P382UUnit,false)
  call P382UMark("unpause",true)
 elseif age==60 and udg_P382UScene==1 then
  call P382UMark("remove-sleep-after-unpause",UnitRemoveAbility(udg_P382UUnit,'BUsl'))
 elseif age==145 then
  call P382UMark("post-resume-visibility",true)
 elseif age==153 then
  call P382UMark("remove-passive",UnitRemoveAbility(udg_P382UUnit,'Apiv'))
 elseif age==155 then
  call RemoveUnit(udg_P382UCaster)
  call RemoveUnit(udg_P382UControl)
  call RemoveUnit(udg_P382UUnit)
  call P382UMark("cleanup",true)
 elseif age==159 then
  if udg_P382UScene==3 then
   call P382UMark("complete",true)
   call PauseTimer(udg_P382UTimer)
  endif
  set udg_P382UScene=udg_P382UScene+1
 endif
 if age<155 then
  call P382UMark("sample",true)
 endif
 set udg_P382UTick=udg_P382UTick+1
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
 call Preload("P382U tick=0 label=start")
 set udg_P382UTimer=CreateTimer()
 call TimerStart(udg_P382UTimer,0.1,true,function P382UTick)
endfunction
