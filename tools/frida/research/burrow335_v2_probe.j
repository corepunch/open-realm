globals
 unit B335U=null
 unit B335C=null
 unit B335Ally=null
 timer B335Timer=null
 integer B335Tick=0
 integer B335Scene=0
endglobals
function B335Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function B335Emit takes string s returns nothing
 local string v="B335 tick="+I2S(B335Tick)+" scene="+I2S(B335Scene)+" "+s
 if StringLength(v)>160 then
  call Preload("B335 tick="+I2S(B335Tick)+" label=OVERSIZE")
 else
  call Preload(v)
 endif
endfunction
function B335State takes nothing returns nothing
 call B335Emit("label=pose x="+R2S(GetUnitX(B335U))+" y="+R2S(GetUnitY(B335U)))
 call B335Emit("label=state inv="+I2S(GetUnitAbilityLevel(B335U,'Binv'))+" sleep="+I2S(GetUnitAbilityLevel(B335U,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(B335U,'BUsp'))+" burrow="+I2S(GetUnitAbilityLevel(B335U,'B335'))+" ww="+I2S(GetUnitAbilityLevel(B335U,'BOwk')))
 call B335Emit("label=visibility invisible="+I2S(B335Bool(IsUnitInvisible(B335U,Player(1))))+" paused="+I2S(B335Bool(IsUnitPaused(B335U)))+" head="+I2S(GetUnitCurrentOrder(B335U)))
 call B335Emit("label=visibility-own invisible="+I2S(B335Bool(IsUnitInvisible(B335U,Player(0))))+" visible="+I2S(B335Bool(IsUnitVisible(B335U,Player(0)))))
 call B335Emit("label=visibility-enemy visible="+I2S(B335Bool(IsUnitVisible(B335U,Player(1)))))
endfunction
function B335Spell takes nothing returns nothing
 local integer role=2
 if GetTriggerUnit()==B335Ally then
  set role=3
 endif
 if GetTriggerUnit()==B335U then
 set role=1
 endif
 call B335Emit("label=spell role="+I2S(role)+" event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId())+" head="+I2S(GetUnitCurrentOrder(B335U)))
 call B335State()
endfunction
function B335Order takes nothing returns nothing
 call B335Emit("label=order id="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(B335U)))
endfunction
function B335Step takes nothing returns nothing
 local integer age=ModuloInteger(B335Tick,100)
 local trigger tr
 if B335Tick==0 then
  call B335Emit("label=start")
  call PreloadGenEnd("rs-burrow335-v2-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call B335Emit("label=start")
 endif
 if age==0 then
  set B335Scene=B335Tick/100
  if B335Scene==2 then
   call B335Emit("label=complete")
   call PreloadGenEnd("rs-burrow335-v2.txt")
   call PauseTimer(B335Timer)
   return
  endif
  set B335U=CreateUnit(Player(0),'ucrm',640,1024,0)
  set B335Ally=CreateUnit(Player(0),'hfoo',900,1024,180)
  set B335C=CreateUnit(Player(1),'hfoo',900,1152,180)
  call SetUnitAcquireRange(B335U,0)
  call SetUnitAcquireRange(B335Ally,0)
  call SetUnitAcquireRange(B335C,0)
  call B335Emit("label=add-detector result="+I2S(B335Bool(UnitAddAbility(B335C,'Atru'))))
  call B335Emit("label=add-invis result="+I2S(B335Bool(UnitAddAbility(B335Ally,'I335'))))
  call B335Emit("label=add-sleep result="+I2S(B335Bool(UnitAddAbility(B335C,'S335'))))
  call B335Emit("label=add-windwalk result="+I2S(B335Bool(UnitAddAbility(B335U,'W335'))))
  call UnitMakeAbilityPermanent(B335U,true,'W335')
  set tr=CreateTrigger()
  call TriggerAddAction(tr,function B335Spell)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerRegisterUnitEvent(tr,B335C,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,B335C,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,B335C,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,B335C,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,B335C,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerRegisterUnitEvent(tr,B335Ally,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,B335Ally,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,B335Ally,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,B335Ally,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,B335Ally,EVENT_UNIT_SPELL_ENDCAST)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerRegisterUnitEvent(tr,B335U,EVENT_UNIT_ISSUED_TARGET_ORDER)
  call TriggerAddAction(tr,function B335Order)
 elseif age==1 then
  if B335Scene==0 then
   call B335Emit("label=invisibility result="+I2S(B335Bool(IssueTargetOrder(B335Ally,"invisibility",B335U))))
  else
   call B335Emit("label=windwalk result="+I2S(B335Bool(IssueImmediateOrder(B335U,"windwalk"))))
  endif
 elseif age==4 then
  call B335Emit("label=add-burrow result="+I2S(B335Bool(UnitAddAbility(B335U,'B335'))))
  call UnitMakeAbilityPermanent(B335U,true,'B335')
 elseif age==20 then
  call B335Emit("label=sleep result="+I2S(B335Bool(IssueTargetOrder(B335C,"sleep",B335U))))
 elseif age==30 then
  call PauseUnit(B335U,true)
  call B335Emit("label=pause")
 elseif age==35 then
  call B335Emit("label=remove-burrow result="+I2S(B335Bool(UnitRemoveAbility(B335U,'B335'))))
 elseif age==40 then
  call PauseUnit(B335U,false)
  call B335Emit("label=resume")

 elseif age==75 then
  if B335Scene==0 then
   call UnitRemoveBuffs(B335U,true,false)
   call B335Emit("label=remove-positive")
  else
   call B335Emit("label=remove-BOwk result="+I2S(B335Bool(UnitRemoveAbility(B335U,'BOwk'))))
  endif
 elseif age==95 then
  call RemoveUnit(B335U)
  call RemoveUnit(B335Ally)
  call RemoveUnit(B335C)
  call B335Emit("label=cleanup")
 endif
 if age<95 then
  call B335Emit("label=type base="+I2S(B335Bool(GetUnitTypeId(B335U)=='ucry'))+" alternate="+I2S(B335Bool(GetUnitTypeId(B335U)=='ucrm')))
  call B335State()
 endif
 set B335Tick=B335Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1366)
 call SetPlayerTechResearched(Player(0),'Rubu',1)
 call FogEnable(false)
 call FogMaskEnable(false)
 set B335Timer=CreateTimer()
 call TimerStart(B335Timer,0.1,true,function B335Step)
endfunction
