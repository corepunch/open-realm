globals
 unit I333U=null
 unit I333T=null
 timer I333Timer=null
 integer I333Tick=0
 integer I333Scene=0
endglobals
function I333Bool takes boolean b returns integer
 if b then
 return 1
 endif
 return 0
endfunction
function I333Emit takes string s returns nothing
 call Preload("I333 tick="+I2S(I333Tick)+" scene="+I2S(I333Scene)+" "+s)
endfunction
function I333State takes nothing returns nothing
 call I333Emit("label=pose x="+R2S(GetUnitX(I333U))+" y="+R2S(GetUnitY(I333U)))
 call I333Emit("label=state head="+I2S(GetUnitCurrentOrder(I333U))+" ww="+I2S(GetUnitAbilityLevel(I333U,'BOwk'))+" invisible="+I2S(I333Bool(IsUnitInvisible(I333U,Player(1)))))
endfunction
function I333Spell takes nothing returns nothing
 call I333Emit("label=spell event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId())+" head="+I2S(GetUnitCurrentOrder(I333U)))
 call I333State()
endfunction
function I333Order takes nothing returns nothing
 call I333Emit("label=order id="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(I333U)))
 call I333State()
endfunction
function I333Step takes nothing returns nothing
 local integer age=ModuloInteger(I333Tick,60)
 local trigger tr
 if I333Tick==0 then
 call I333Emit("label=start")
 call PreloadGenEnd("rs-invisibility333-start.txt")
 call PreloadGenClear()
 call PreloadGenStart()
 call I333Emit("label=start")
 endif
 if age==0 then
  if I333U!=null then
   call RemoveUnit(I333U)
   call RemoveUnit(I333T)
  endif
  set I333Scene=I333Tick/60
  if I333Scene==4 then
   call I333Emit("label=complete")
   call PreloadGenEnd("rs-invisibility333.txt")
   call PauseTimer(I333Timer)
   return
  endif
  set I333U=CreateUnit(Player(0),'Hpal',1024,512,180)
  set I333T=CreateUnit(Player(1),'hfoo',1900,512,180)
  call SetUnitAcquireRange(I333U,51)
  call SetUnitAcquireRange(I333T,51)
  // Keep the distant held target vulnerable so explicit Attack can be admitted.
  call IssueImmediateOrder(I333T,"holdposition")
  if I333Scene==3 then
   call I333Emit("label=add-custom result="+I2S(I333Bool(UnitAddAbility(I333U,'W333'))))
  else
   call I333Emit("label=add-stock result="+I2S(I333Bool(UnitAddAbility(I333U,'AOwk'))))
  endif
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerAddAction(tr,function I333Spell)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerRegisterUnitEvent(tr,I333U,EVENT_UNIT_ISSUED_TARGET_ORDER)
  call TriggerAddAction(tr,function I333Order)
 elseif age==1 then
  if I333Scene==1 or I333Scene==3 then
   call I333Emit("label=move result="+I2S(I333Bool(IssuePointOrder(I333U,"move",1900,512))))
  elseif I333Scene==2 then
   call I333Emit("label=attack result="+I2S(I333Bool(IssueTargetOrder(I333U,"attack",I333T))))
  endif
 elseif age==5 then
  call I333Emit("label=before-windwalk")
  call I333State()
  call I333Emit("label=windwalk result="+I2S(I333Bool(IssueImmediateOrder(I333U,"windwalk"))))
  call I333Emit("label=after-windwalk")
 endif
 call I333State()
 set I333Tick=I333Tick+1
endfunction
function I333Init takes nothing returns nothing
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set I333Timer=CreateTimer()
 call TimerStart(I333Timer,0.1,true,function I333Step)
endfunction
