globals
 timer udg_P382STimer=null
 unit udg_P382SUnit=null
 unit udg_P382SCaster=null
 unit udg_P382SControl=null
 integer udg_P382STick=0
 integer udg_P382SScene=0
 integer udg_P382SAbility='AEme'
 integer udg_P382SSetting=0
endglobals
function P382SMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 if IsUnitPaused(udg_P382SUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382SUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382S setting="+I2S(udg_P382SSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382STick)+" scene="+I2S(udg_P382SScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382SUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382SUnit,udg_P382SAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382SUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382SUnit,'BEme'))+" sleep="+I2S(GetUnitAbilityLevel(udg_P382SUnit,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(udg_P382SUnit,'BUsp'))+" controlSleep="+I2S(GetUnitAbilityLevel(udg_P382SControl,'BUsl'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382SUnit)))
 call PreloadGenEnd("payoff382s.txt")
endfunction
function P382STick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382STick,160)
 local integer mode=0
 if age==0 then
  if mode==1 then
   set udg_P382SUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382SUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  set udg_P382SCaster=CreateUnit(Player(2),'Udre',900,1024,180)
  set udg_P382SControl=CreateUnit(Player(0),'Edem',1100,1024,0)
  call SetUnitAcquireRange(udg_P382SCaster,0)
  call SetUnitAcquireRange(udg_P382SControl,0)
  call SetUnitAcquireRange(udg_P382SUnit,0)
  call SetUnitState(udg_P382SUnit,UNIT_STATE_MANA,1000)
  call P382SMark("created",true)
 elseif age==1 then
  call P382SMark("add-morph",UnitAddAbility(udg_P382SUnit,udg_P382SAbility))
  call UnitMakeAbilityPermanent(udg_P382SUnit,true,udg_P382SAbility)
  call P382SMark("add-sleep-producer",UnitAddAbility(udg_P382SCaster,'AUsl'))
  call SetUnitState(udg_P382SCaster,UNIT_STATE_MANA,1000)
 elseif age==2 then
  call P382SMark("add-passive-before",UnitAddAbility(udg_P382SUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382SUnit,true,'Apiv')
 elseif age==5 then
  call P382SMark("independent-sleep-positive",IssueTargetOrder(udg_P382SCaster,"sleep",udg_P382SControl))
 elseif age==10 then
  call P382SMark("independent-sleep-witness",true)
 elseif age==11 and mode==0 then
  call P382SMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382SMark("forward",IssueImmediateOrder(udg_P382SUnit,"metamorphosis"))
 elseif age==18 then
  call P382SMark("remove-positive-sleep",UnitRemoveAbility(udg_P382SControl,'BUsl'))
 elseif age==20 then
  call P382SMark("sleep-morphed-actor",IssueTargetOrder(udg_P382SCaster,"sleep",udg_P382SUnit))
 elseif age==22 and udg_P382SScene<3 then
  call PauseUnit(udg_P382SUnit,true)
  call P382SMark("pause",true)
 elseif age==23 and udg_P382SScene==2 then
  call P382SMark("remove-sleep-while-paused",UnitRemoveAbility(udg_P382SUnit,'BUsl'))
 elseif age==50 then
  call P382SMark("held-across-natural-lifetime",true)
 elseif age==55 and udg_P382SScene<3 then
  call PauseUnit(udg_P382SUnit,false)
  call P382SMark("unpause",true)
 elseif age==60 and udg_P382SScene==1 then
  call P382SMark("remove-sleep-after-unpause",UnitRemoveAbility(udg_P382SUnit,'BUsl'))
 elseif age==145 then
  call P382SMark("post-resume-visibility",true)
 elseif age==153 then
  call P382SMark("remove-passive",UnitRemoveAbility(udg_P382SUnit,'Apiv'))
 elseif age==155 then
  call RemoveUnit(udg_P382SCaster)
  call RemoveUnit(udg_P382SControl)
  call RemoveUnit(udg_P382SUnit)
  call P382SMark("cleanup",true)
 elseif age==159 then
  if udg_P382SScene==3 then
   call P382SMark("complete",true)
   call PauseTimer(udg_P382STimer)
  endif
  set udg_P382SScene=udg_P382SScene+1
 endif
 if age<155 then
  call P382SMark("sample",true)
 endif
 set udg_P382STick=udg_P382STick+1
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
 call Preload("P382S tick=0 label=start")
 set udg_P382STimer=CreateTimer()
 call TimerStart(udg_P382STimer,0.1,true,function P382STick)
endfunction
