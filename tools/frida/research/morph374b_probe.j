globals
 timer udg_M374BTimer=null
 unit udg_M374BUnit=null
 integer udg_M374BTick=0
 integer udg_M374BScene=0
 integer udg_M374BAbility='Abrf'
 integer udg_M374BSetting=-1
endglobals
function M374BMark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_M374BUnit,Player(1)) then
  set invisible=1
 endif
 call Preload("M374B setting="+I2S(udg_M374BSetting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_M374BTick)+" scene="+I2S(udg_M374BScene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_M374BUnit))+" morph="+I2S(GetUnitAbilityLevel(udg_M374BUnit,udg_M374BAbility))+" passive="+I2S(GetUnitAbilityLevel(udg_M374BUnit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_M374BUnit)))
 call PreloadGenEnd("morph374b.txt")
endfunction
function M374BTick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M374BTick,70)
 local integer family=udg_M374BScene/3
 local integer mode=ModuloInteger(udg_M374BScene,3)
 if age==0 then
  if family==0 then
   set udg_M374BAbility='Abrf'
   if mode==2 then
    set udg_M374BUnit=CreateUnit(Player(0),'edcm',640,1024,0)
   else
    set udg_M374BUnit=CreateUnit(Player(0),'edoc',640,1024,0)
   endif
  else
   set udg_M374BAbility='Arav'
   if mode==2 then
    set udg_M374BUnit=CreateUnit(Player(0),'edtm',640,1024,0)
   else
    set udg_M374BUnit=CreateUnit(Player(0),'edot',640,1024,0)
   endif
  endif
  call SetUnitAcquireRange(udg_M374BUnit,0)
  call SetUnitState(udg_M374BUnit,UNIT_STATE_MANA,1000)
  call M374BMark("created",true)
 elseif age==1 then
  call M374BMark("add-morph",UnitAddAbility(udg_M374BUnit,udg_M374BAbility))
  call UnitMakeAbilityPermanent(udg_M374BUnit,true,udg_M374BAbility)
 elseif age==2 and mode==0 then
  call M374BMark("add-passive-before",UnitAddAbility(udg_M374BUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M374BUnit,true,'Apiv')
 elseif age==11 and mode==0 then
  call M374BMark("pre-forward-visibility",true)
 elseif age==12 and mode!=2 then
  if family==0 then
   call M374BMark("forward",IssueImmediateOrder(udg_M374BUnit,"bearform"))
  else
   call M374BMark("forward",IssueImmediateOrder(udg_M374BUnit,"ravenform"))
  endif
 elseif age==22 and mode!=0 then
  call M374BMark("add-passive-after",UnitAddAbility(udg_M374BUnit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M374BUnit,true,'Apiv')
 elseif age==35 then
  if family==0 then
   call M374BMark("reverse",IssueImmediateOrder(udg_M374BUnit,"unbearform"))
  else
   call M374BMark("reverse",IssueImmediateOrder(udg_M374BUnit,"unravenform"))
  endif
 elseif age==50 then
  call M374BMark("post-reverse-visibility",true)
 elseif age==60 then
  call M374BMark("remove-passive",UnitRemoveAbility(udg_M374BUnit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_M374BUnit)
  call M374BMark("cleanup",true)
 elseif age==69 then
  if udg_M374BScene==5 then
   call M374BMark("complete",true)
   call PauseTimer(udg_M374BTimer)
  endif
  set udg_M374BScene=udg_M374BScene+1
 endif
 if age<65 then
  call M374BMark("sample",true)
 endif
 set udg_M374BTick=udg_M374BTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1374)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_CONTROL,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_CONTROL,false)
 call Preload("M374B tick=0 label=start")
 set udg_M374BTimer=CreateTimer()
 call TimerStart(udg_M374BTimer,0.1,true,function M374BTick)
endfunction
