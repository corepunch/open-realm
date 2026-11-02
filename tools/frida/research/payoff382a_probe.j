globals
 timer udg_P382ATimer=null
 unit udg_P382AUnit=null
 integer udg_P382ATick=0
 integer udg_P382AScene=0
 integer udg_P382AAbility='AM82'
 integer udg_P382ASetting=0
endglobals
function P382AMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_P382AUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("P382A setting="+I2S(udg_P382ASetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_P382ATick)+" scene="+I2S(udg_P382AScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_P382AUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_P382AUnit,udg_P382AAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_P382AUnit,'Apiv'))+" buff="+I2S(GetUnitAbilityLevel(udg_P382AUnit,'Bblo'))+" basebuff="+I2S(GetUnitAbilityLevel(udg_P382AUnit,'BEme'))+" order="+I2S(GetUnitCurrentOrder(udg_P382AUnit)))
 call PreloadGenEnd("payoff382a.txt")
endfunction
function P382ATick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P382ATick,70)
 local integer mode=ModuloInteger(udg_P382AScene,3)
 if age==0 then
  if mode==2 then
   set udg_P382AUnit=CreateUnit(Player(0),'Edmm',640,1024,0)
  else
   set udg_P382AUnit=CreateUnit(Player(0),'Edem',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_P382AUnit,0)
  call SetUnitState(udg_P382AUnit,UNIT_STATE_MANA,1000)
  call P382AMark("created",true)
 elseif age==1 then
  call P382AMark("add-morph",UnitAddAbility(udg_P382AUnit,udg_P382AAbility))
  call UnitMakeAbilityPermanent(udg_P382AUnit,true,udg_P382AAbility)
 elseif age==2 and mode==0 then
  call P382AMark("add-passive-before",UnitAddAbility(udg_P382AUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382AUnit,true,'Apiv')
 elseif age==11 and mode==0 then
  call P382AMark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  call P382AMark("forward",IssueImmediateOrder(udg_P382AUnit,"metamorphosis"))
 elseif age==22 and mode!=0 then
  call P382AMark("add-passive-after",UnitAddAbility(udg_P382AUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_P382AUnit,true,'Apiv')
 elseif age==23 then
  call P382AMark("remove-metamorphosis-buff",UnitRemoveAbility(udg_P382AUnit,'Bblo'))
 elseif age==35 then
  call P382AMark("natural-lifetime-sample",true)
 elseif age==50 then
  call P382AMark("post-reverse-visibility",true)
 elseif age==60 then
  call P382AMark("remove-passive",UnitRemoveAbility(udg_P382AUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_P382AUnit)
  call P382AMark("cleanup",true)
 elseif age==69 then
  if udg_P382AScene==2 then
   call P382AMark("complete",true)
   call PauseTimer(udg_P382ATimer)
  endif
  set udg_P382AScene=udg_P382AScene+1
 endif
 if age<65 then
  call P382AMark("sample",true)
 endif
 set udg_P382ATick=udg_P382ATick+1
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
 call Preload("P382A tick=0 label=start")
 set udg_P382ATimer=CreateTimer()
 call TimerStart(udg_P382ATimer,0.1,true,function P382ATick)
endfunction
