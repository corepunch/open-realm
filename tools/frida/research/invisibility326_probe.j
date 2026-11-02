globals
 unit I326U=null
 item I326Item=null
 timer I326Timer=null
 integer I326Tick=0
 integer I326Scene=0
 boolean I326Acted=false
endglobals
function I326Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function I326Emit takes string s returns nothing
 local string v="I326 tick="+I2S(I326Tick)+" scene="+I2S(I326Scene)+" "+s
 if StringLength(v)>160 then
  call Preload("I326 label=OVERSIZE")
 else
  call Preload(v)
 endif
endfunction
function I326State takes nothing returns nothing
 call I326Emit("label=pose x="+R2S(GetUnitX(I326U))+" y="+R2S(GetUnitY(I326U)))
 call I326Emit("label=state head="+I2S(GetUnitCurrentOrder(I326U))+" binv="+I2S(GetUnitAbilityLevel(I326U,'Binv'))+" item="+I2S(GetItemTypeId(I326Item))+" slot="+I2S(I326Bool(UnitItemInSlot(I326U,0)==I326Item)))
endfunction
function I326Event takes nothing returns nothing
 local eventid e=GetTriggerEventId()
 call I326Emit("label=event id="+I2S(GetHandleId(e))+" code="+I2S(GetSpellAbilityId())+" used="+I2S(GetItemTypeId(GetManipulatedItem())))
 call I326State()
 if not I326Acted and ((I326Scene==1 and e==EVENT_UNIT_SPELL_CHANNEL) or (I326Scene==2 and e==EVENT_UNIT_SPELL_CAST) or (I326Scene==3 and e==EVENT_UNIT_SPELL_EFFECT)) then
  set I326Acted=true
  call I326Emit("label=callback-stop result="+I2S(I326Bool(IssueImmediateOrder(I326U,"stop"))))
 elseif not I326Acted and I326Scene==4 and e==EVENT_UNIT_SPELL_CHANNEL then
  set I326Acted=true
  call RemoveItem(I326Item)
  call I326Emit("label=callback-remove-item")
 endif
endfunction
function I326Order takes nothing returns nothing
 call I326Emit("label=order id="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(I326U)))
endfunction
function I326Step takes nothing returns nothing
 local integer age=ModuloInteger(I326Tick,12)
 local trigger tr
 if I326Tick==0 then
  call I326Emit("label=start")
  call PreloadGenEnd("rs-invisibility326-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call I326Emit("label=start")
 endif
 if age==0 then
  if I326U!=null then
   call RemoveUnit(I326U)
   call RemoveItem(I326Item)
  endif
  set I326Scene=I326Tick/12
  if I326Scene==5 then
   call I326Emit("label=complete")
   call PreloadGenEnd("rs-invisibility326.txt")
   call PauseTimer(I326Timer)
   return
  endif
  set I326Acted=false
  set I326U=CreateUnit(Player(0),'Hpal',1024,512,0)
  call SetUnitAcquireRange(I326U,51)
  set I326Item=CreateItem('pinv',1024,512)
  call I326Emit("label=inventory level="+I2S(GetUnitAbilityLevel(I326U,'AInv')))
  call I326Emit("label=pickup result="+I2S(I326Bool(UnitAddItem(I326U,I326Item))))
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_USE_ITEM)
  call TriggerAddAction(tr,function I326Event)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(tr,I326U,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerAddAction(tr,function I326Order)
  if I326Scene==0 then
   call I326Emit("label=move result="+I2S(I326Bool(IssuePointOrder(I326U,"move",1900,512))))
  endif
 elseif age==1 then
  call I326Emit("label=before-use")
  call I326State()
  call I326Emit("label=use result="+I2S(I326Bool(UnitUseItem(I326U,I326Item))))
  call I326Emit("label=after-use")
 endif
 call I326State()
 set I326Tick=I326Tick+1
endfunction
function I326Init takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set I326Timer=CreateTimer()
 call TimerStart(I326Timer,0.5,true,function I326Step)
endfunction
