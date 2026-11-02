globals
 timer udg_P382TTimer=null
 unit udg_P382TUnit=null
 unit udg_P382TCaster=null
 unit udg_P382TControl=null
 integer udg_P382TTick=0
 integer udg_P382TScene=0
 integer udg_P382TAbility='AEme'
 integer udg_P382TSetting=0
endglobals
function P382TMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 local integer paused=0
 if IsUnitPaused(udg_P382TUnit) then
  set paused=1
 endif
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382TUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382T setting="+I2S(udg_P382TSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382TTick)+" scene="+I2S(udg_P382TScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382TUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382TUnit,udg_P382TAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382TUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382TUnit,'BEme'))+" sleep="+I2S(GetUnitAbilityLevel(udg_P382TUnit,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(udg_P382TUnit,'BUsp'))+" controlSleep="+I2S(GetUnitAbilityLevel(udg_P382TControl,'BUsl'))+" paused="+I2S(paused)+" order="+I2S(GetUnitCurrentOrder(udg_P382TUnit)))
 call PreloadGenEnd("payoff382t.txt")
endfunction
function P382TTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382TTick,160)
 local integer mode=0
 if age==0 then
  if mode==1 then
   set udg_P382TUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382TUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  set udg_P382TCaster=CreateUnit(Player(2),'Udre',900,1024,180)
  set udg_P382TControl=CreateUnit(Player(0),'Edem',1100,1024,0)
  call SetUnitAcquireRange(udg_P382TCaster,0)
  call SetUnitAcquireRange(udg_P382TControl,0)
  call SetUnitAcquireRange(udg_P382TUnit,0)
  call SetUnitState(udg_P382TUnit,UNIT_STATE_MANA,1000)
  call P382TMark("created",true)
 elseif age==1 then
  call P382TMark("add-morph",UnitAddAbility(udg_P382TUnit,udg_P382TAbility))
  call UnitMakeAbilityPermanent(udg_P382TUnit,true,udg_P382TAbility)
  call P382TMark("add-sleep-producer",UnitAddAbility(udg_P382TCaster,'AUsl'))
  call SetUnitState(udg_P382TCaster,UNIT_STATE_MANA,1000)
 elseif age==2 then
  call P382TMark("add-passive-before",UnitAddAbility(udg_P382TUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382TUnit,true,'Apiv')
 elseif age==5 then
  call P382TMark("independent-sleep-positive",IssueTargetOrder(udg_P382TCaster,"sleep",udg_P382TControl))
 elseif age==10 then
  call P382TMark("independent-sleep-witness",true)
 elseif age==11 and mode==0 then
  call P382TMark("pre-forward-visibility",true)
 elseif age==12 and mode!=1 then
  call P382TMark("forward",IssueImmediateOrder(udg_P382TUnit,"metamorphosis"))
 elseif age==18 then
  call P382TMark("remove-positive-sleep",UnitRemoveAbility(udg_P382TControl,'BUsl'))
 elseif age==20 then
  call P382TMark("sleep-morphed-actor",IssueTargetOrder(udg_P382TCaster,"sleep",udg_P382TUnit))
 elseif age==22 and udg_P382TScene<3 then
  call PauseUnit(udg_P382TUnit,true)
  call P382TMark("pause",true)
 elseif age==23 and udg_P382TScene==2 then
  call P382TMark("remove-sleep-while-paused",UnitRemoveAbility(udg_P382TUnit,'BUsl'))
 elseif age==50 then
  call P382TMark("held-across-natural-lifetime",true)
 elseif age==55 and udg_P382TScene<3 then
  call PauseUnit(udg_P382TUnit,false)
  call P382TMark("unpause",true)
 elseif age==60 and udg_P382TScene==1 then
  call P382TMark("remove-sleep-after-unpause",UnitRemoveAbility(udg_P382TUnit,'BUsl'))
 elseif age==145 then
  call P382TMark("post-resume-visibility",true)
 elseif age==153 then
  call P382TMark("remove-passive",UnitRemoveAbility(udg_P382TUnit,'Apiv'))
 elseif age==155 then
  call RemoveUnit(udg_P382TCaster)
  call RemoveUnit(udg_P382TControl)
  call RemoveUnit(udg_P382TUnit)
  call P382TMark("cleanup",true)
 elseif age==159 then
  if udg_P382TScene==3 then
   call P382TMark("complete",true)
   call PauseTimer(udg_P382TTimer)
  endif
  set udg_P382TScene=udg_P382TScene+1
 endif
 if age<155 then
  call P382TMark("sample",true)
 endif
 set udg_P382TTick=udg_P382TTick+1
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
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,true)
 call Preload("P382T tick=0 label=start")
 set udg_P382TTimer=CreateTimer()
 call TimerStart(udg_P382TTimer,0.1,true,function P382TTick)
endfunction
