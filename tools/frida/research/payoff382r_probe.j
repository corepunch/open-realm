globals
 timer udg_P382RTimer=null
 unit udg_P382RUnit=null
 integer udg_P382RTick=0
 integer udg_P382RScene=0
 integer udg_P382RAbility='AEme'
 integer udg_P382RSetting=0
endglobals
function P382RMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382RUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382R setting="+I2S(udg_P382RSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382RTick)+" scene="+I2S(udg_P382RScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382RUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382RUnit,udg_P382RAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382RUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382RUnit,'BEme'))+" order="+I2S(GetUnitCurrentOrder(udg_P382RUnit)))
 call PreloadGenEnd("payoff382r.txt")
endfunction
function P382RTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382RTick,70)
 local integer mode=ModuloInteger(udg_P382RScene,3)
 if age==0 then
  if mode==2 then
   set udg_P382RUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382RUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P382RUnit,0)
  call SetUnitState(udg_P382RUnit,UNIT_STATE_MANA,1000)
  call P382RMark("created",true)
 elseif age==1 then
  call P382RMark("add-morph",UnitAddAbility(udg_P382RUnit,udg_P382RAbility))
  call UnitMakeAbilityPermanent(udg_P382RUnit,true,udg_P382RAbility)
 elseif age==2 and mode==0 then
  call P382RMark("add-passive-before",UnitAddAbility(udg_P382RUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382RUnit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P382RMark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P382RMark("forward",IssueImmediateOrder(udg_P382RUnit,"metamorphosis"))
 elseif age==22 and mode!=0 then
  call P382RMark("add-passive-after",UnitAddAbility(udg_P382RUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382RUnit,true,'Apiv')
 elseif age==23 then
  call P382RMark("remove-metamorphosis-buff",UnitRemoveAbility(udg_P382RUnit,'BEme'))
 elseif age==35 then
  call P382RMark("natural-lifetime-sample",true)
 elseif age==50 then
  call P382RMark("post-reverse-visibility",true)
 elseif age==60 then
  call P382RMark("remove-passive",UnitRemoveAbility(udg_P382RUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P382RUnit)
  call P382RMark("cleanup",true)
 elseif age==69 then
  if udg_P382RScene==2 then
   call P382RMark("complete",true)
   call PauseTimer(udg_P382RTimer)
  endif
  set udg_P382RScene=udg_P382RScene+1
 endif
 if age<65 then
  call P382RMark("sample",true)
 endif
 set udg_P382RTick=udg_P382RTick+1
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
 call Preload("P382R tick=0 label=start")
 set udg_P382RTimer=CreateTimer()
 call TimerStart(udg_P382RTimer,0.1,true,function P382RTick)
endfunction
