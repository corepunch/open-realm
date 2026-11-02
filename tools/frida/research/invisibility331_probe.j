globals
 unit I331U=null
 unit I331C=null
 item I331Item=null
 timer I331Timer=null
 integer I331Tick=0
 integer I331Scene=0
endglobals
function I331Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function I331Emit takes string s returns nothing
 local string v="I331 tick="+I2S(I331Tick)+" scene="+I2S(I331Scene)+" "+s
 if StringLength(v)>160 then
  call Preload("I331 tick="+I2S(I331Tick)+" label=OVERSIZE")
 else
  call Preload(v)
 endif
endfunction
function I331State takes nothing returns nothing
 call I331Emit("label=pose x="+R2S(GetUnitX(I331U))+" y="+R2S(GetUnitY(I331U)))
 call I331Emit("label=state inv="+I2S(GetUnitAbilityLevel(I331U,'Binv'))+" sleep="+I2S(GetUnitAbilityLevel(I331U,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(I331U,'BUsp'))+" ghost="+I2S(GetUnitAbilityLevel(I331U,'Agho'))+" ww="+I2S(GetUnitAbilityLevel(I331U,'BOwk')))
 call I331Emit("label=visibility invisible="+I2S(I331Bool(IsUnitInvisible(I331U,Player(1))))+" paused="+I2S(I331Bool(IsUnitPaused(I331U)))+" head="+I2S(GetUnitCurrentOrder(I331U)))
 call I331Emit("label=visibility-own invisible="+I2S(I331Bool(IsUnitInvisible(I331U,Player(0))))+" visible="+I2S(I331Bool(IsUnitVisible(I331U,Player(0)))))
 call I331Emit("label=visibility-enemy visible="+I2S(I331Bool(IsUnitVisible(I331U,Player(1)))))
endfunction
function I331Spell takes nothing returns nothing
 local integer role=2
 if GetTriggerUnit()==I331U then
 set role=1
 endif
 call I331Emit("label=spell role="+I2S(role)+" event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId())+" head="+I2S(GetUnitCurrentOrder(I331U)))
 call I331State()
endfunction
function I331Order takes nothing returns nothing
 call I331Emit("label=order id="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(I331U)))
endfunction
function I331Step takes nothing returns nothing
 local integer age=ModuloInteger(I331Tick,24)
 local trigger tr
 if I331Tick==0 then
 call I331Emit("label=start")
 call PreloadGenEnd("rs-invisibility331-start.txt")
 call PreloadGenClear()
 call PreloadGenStart()
 call I331Emit("label=start")
 endif
 if age==0 then
  if I331U!=null then
   call RemoveUnit(I331U)
   call RemoveUnit(I331C)
   call RemoveItem(I331Item)
  endif
  set I331Scene=I331Tick/24
  if I331Scene==4 then
   call I331Emit("label=complete")
   call PreloadGenEnd("rs-invisibility331.txt")
   call PauseTimer(I331Timer)
   return
  endif
  set I331U=CreateUnit(Player(0),'Hpal',1024,512,0)
  set I331C=CreateUnit(Player(1),'hfoo',1400,512,180)
  call SetUnitAcquireRange(I331U,51)
  call I331Emit("label=add-windwalk result="+I2S(I331Bool(UnitAddAbility(I331U,'AOwk'))))
  call SetUnitAcquireRange(I331C,51)
  call I331Emit("label=inventory level="+I2S(GetUnitAbilityLevel(I331U,'AInv')))
  call I331Emit("label=ability result="+I2S(I331Bool(UnitAddAbility(I331C,'A324'))))
  set I331Item=CreateItem('pinv',1024,512)
  call I331Emit("label=pickup result="+I2S(I331Bool(UnitAddItem(I331U,I331Item))))
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I331C,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,I331C,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,I331C,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,I331C,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,I331C,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerAddAction(tr,function I331Spell)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_USE_ITEM)
  call TriggerAddAction(tr,function I331Spell)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(tr,I331U,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerAddAction(tr,function I331Order)
 elseif age==1 then
  if I331Scene==1 then
   call I331Emit("label=windwalk result="+I2S(I331Bool(IssueImmediateOrder(I331U,"windwalk"))))
  elseif I331Scene==2 then
   call I331Emit("label=add-ghost result="+I2S(I331Bool(UnitAddAbility(I331U,'Agho'))))
  else
   call I331Emit("label=use result="+I2S(I331Bool(UnitUseItem(I331U,I331Item))))
  endif
 elseif age==2 then
  if I331Scene==0 then
   call I331Emit("label=windwalk result="+I2S(I331Bool(IssueImmediateOrder(I331U,"windwalk"))))
  elseif I331Scene==3 then
   call I331Emit("label=add-ghost result="+I2S(I331Bool(UnitAddAbility(I331U,'Agho'))))
   call I331Emit("label=windwalk result="+I2S(I331Bool(IssueImmediateOrder(I331U,"windwalk"))))
  else
   call I331Emit("label=use result="+I2S(I331Bool(UnitUseItem(I331U,I331Item))))
  endif
 elseif age==3 then
  call I331Emit("label=sleep-order result="+I2S(I331Bool(IssueTargetOrder(I331C,"sleep",I331U))))
 elseif age==5 then
  call PauseUnit(I331U,true)
  call I331Emit("label=pause")
 elseif age==10 then
  call UnitRemoveBuffs(I331U,false,true)
  call I331Emit("label=remove-negative")
 elseif age==11 then
  if I331Scene==0 then
   call I331Emit("label=remove-ww result="+I2S(I331Bool(UnitRemoveAbility(I331U,'BOwk'))))
  elseif I331Scene==2 then
   call I331Emit("label=remove-ghost result="+I2S(I331Bool(UnitRemoveAbility(I331U,'Agho'))))
  endif
 elseif age==14 then
  call PauseUnit(I331U,false)
  call I331Emit("label=unpause")
 elseif age==21 then
  call I331Emit("label=remove-ghost-final result="+I2S(I331Bool(UnitRemoveAbility(I331U,'Agho'))))
  call I331Emit("label=remove-ww-final result="+I2S(I331Bool(UnitRemoveAbility(I331U,'BOwk'))))
 endif
 call I331State()
 set I331Tick=I331Tick+1
endfunction
function I331Init takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set I331Timer=CreateTimer()
 call TimerStart(I331Timer,0.5,true,function I331Step)
endfunction
