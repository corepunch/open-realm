globals
 timer udg_P382WTimer=null
 unit udg_P382WUnit=null
 unit udg_P382WCaster=null
 unit udg_P382WPositiveCaster=null
 unit udg_P382WControl=null
 integer udg_P382WTick=0
 integer udg_P382WScene=0
 integer udg_P382WAbility='AEme'
 integer udg_P382WSetting=0
endglobals
function P382WMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 local integer immune=0
 if IsUnitType(udg_P382WUnit,UNIT_TYPE_MAGIC_IMMUNE) then
  set immune=1
 endif
 if IsUnitPaused(udg_P382WUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382WUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382W setting="+I2S(udg_P382WSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382WTick)+" scene="+I2S(udg_P382WScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382WUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382WUnit,udg_P382WAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382WUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382WUnit,'BEme'))+" immune="+I2S(immune)+" sleep="+I2S(GetUnitAbilityLevel(udg_P382WUnit,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(udg_P382WUnit,'BUsp'))+" controlSleep="+I2S(GetUnitAbilityLevel(udg_P382WControl,'BUsl'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382WUnit)))
 call PreloadGenEnd("payoff382w.txt")
endfunction
function P382WTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382WTick,160)
 local integer mode=0
 if age==0 then
  if mode==1 then
   set udg_P382WUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382WUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  set udg_P382WCaster=CreateUnit(Player(2),'Udre',900,1024,180)
  set udg_P382WPositiveCaster=CreateUnit(Player(2),'Udre',1350,1024,180)
  call SetUnitInvulnerable(udg_P382WPositiveCaster,true)
  call SetUnitAcquireRange(udg_P382WPositiveCaster,0)
  call UnitAddAbility(udg_P382WPositiveCaster,'AUsl')
  call SetUnitAbilityLevel(udg_P382WPositiveCaster,'AUsl',1)
  call SetUnitState(udg_P382WPositiveCaster,UNIT_STATE_MANA,1000)
  set udg_P382WControl=CreateUnit(Player(0),'Edem',1100,1024,0)
  call SetUnitInvulnerable(udg_P382WCaster,true)
  call UnitAddAbility(udg_P382WCaster,'Adt1')
  call SetUnitAcquireRange(udg_P382WCaster,0)
  call SetUnitAcquireRange(udg_P382WControl,0)
  call SetUnitAcquireRange(udg_P382WUnit,0)
  call SetUnitState(udg_P382WUnit,UNIT_STATE_MANA,1000)
  call P382WMark("created",true)
 elseif age==1 then
  call P382WMark("add-morph",UnitAddAbility(udg_P382WUnit,udg_P382WAbility))
  call UnitMakeAbilityPermanent(udg_P382WUnit,true,udg_P382WAbility)
  call P382WMark("add-sleep-producer",UnitAddAbility(udg_P382WCaster,'AUsl'))
  call SetUnitAbilityLevel(udg_P382WCaster,'AUsl',1)
  call SetUnitState(udg_P382WCaster,UNIT_STATE_MANA,1000)
 elseif age==2 then
  call P382WMark("add-passive-before",UnitAddAbility(udg_P382WUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382WUnit,true,'Apiv')
 elseif age==5 then
  call P382WMark("independent-sleep-positive",IssueTargetOrder(udg_P382WPositiveCaster,"sleep",udg_P382WControl))
 elseif age==10 then
  call P382WMark("independent-sleep-witness",true)
 elseif age==11 and mode==0 then
  call P382WMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382WMark("forward",IssueImmediateOrder(udg_P382WUnit,"metamorphosis"))
 elseif age==18 then
  call P382WMark("remove-positive-sleep",UnitRemoveAbility(udg_P382WControl,'BUsl'))
 elseif age==16 then
  call P382WMark("sleep-morphed-actor",IssueTargetOrder(udg_P382WCaster,"sleep",udg_P382WUnit))
 elseif age==21 then
  call P382WMark("sleep-before-script-pause",true)
 elseif age==22 and udg_P382WScene<3 then
  call PauseUnit(udg_P382WUnit,true)
  call P382WMark("pause",true)
 elseif age==30 and udg_P382WScene==2 then
  call P382WMark("remove-sleep-while-paused",UnitRemoveAbility(udg_P382WUnit,'BUsl'))
 elseif age==50 then
  call P382WMark("held-across-natural-lifetime",true)
 elseif age==55 and udg_P382WScene<3 then
  call PauseUnit(udg_P382WUnit,false)
  call P382WMark("unpause",true)
 elseif age==60 and udg_P382WScene==1 then
  call P382WMark("remove-sleep-after-unpause",UnitRemoveAbility(udg_P382WUnit,'BUsl'))
 elseif age==145 then
  call P382WMark("post-resume-visibility",true)
 elseif age==153 then
  call P382WMark("remove-passive",UnitRemoveAbility(udg_P382WUnit,'Apiv'))
 elseif age==155 then
  call RemoveUnit(udg_P382WPositiveCaster)
  call RemoveUnit(udg_P382WCaster)
  call RemoveUnit(udg_P382WControl)
  call RemoveUnit(udg_P382WUnit)
  call P382WMark("cleanup",true)
 elseif age==159 then
  if udg_P382WScene==3 then
   call P382WMark("complete",true)
   call PauseTimer(udg_P382WTimer)
  endif
  set udg_P382WScene=udg_P382WScene+1
 endif
 if age<155 then
  call P382WMark("sample",true)
 endif
 set udg_P382WTick=udg_P382WTick+1
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
 call Preload("P382W tick=0 label=start")
 set udg_P382WTimer=CreateTimer()
 call TimerStart(udg_P382WTimer,0.1,true,function P382WTick)
endfunction
