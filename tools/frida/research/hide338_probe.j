globals
 unit H338U=null
 timer H338Timer=null
 integer H338Tick=0
 integer H338Scene=0
endglobals
function H338Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function H338Emit takes string s returns nothing
 local string v="H338 tick="+I2S(H338Tick)+" scene="+I2S(H338Scene)+" "+s
 if StringLength(v)>160 then
  call Preload("H338 label=OVERSIZE")
 else
  call Preload(v)
 endif
endfunction
function H338State takes nothing returns nothing
 call H338Emit("label=pose x="+R2S(GetUnitX(H338U))+" y="+R2S(GetUnitY(H338U)))
 call H338Emit("label=abilities Ahid="+I2S(GetUnitAbilityLevel(H338U,'Ahid'))+" Ashm="+I2S(GetUnitAbilityLevel(H338U,'Ashm'))+" Apiv="+I2S(GetUnitAbilityLevel(H338U,'Apiv')))
 call H338Emit("label=visibility invisible="+I2S(H338Bool(IsUnitInvisible(H338U,Player(1))))+" visible="+I2S(H338Bool(IsUnitVisible(H338U,Player(1))))+" head="+I2S(GetUnitCurrentOrder(H338U)))
 call H338Emit("label=time value="+R2S(GetFloatGameState(GAME_STATE_TIME_OF_DAY)))
endfunction
function H338Order takes nothing returns nothing
 call H338Emit("label=order id="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(H338U)))
endfunction
function H338Spell takes nothing returns nothing
 call H338Emit("label=spell event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId())+" head="+I2S(GetUnitCurrentOrder(H338U)))
endfunction
function H338Step takes nothing returns nothing
 local integer age=ModuloInteger(H338Tick,50)
 local trigger tr
 if H338Tick==0 then
  call H338Emit("label=start")
  call PreloadGenEnd("rs-hide338-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call H338Emit("label=start")
 endif
 if age==0 then
  set H338Scene=H338Tick/50
  if H338Scene==4 then
   call H338Emit("label=complete")
   call PreloadGenEnd("rs-hide338.txt")
   call PauseTimer(H338Timer)
   return
  endif
  if H338Scene==0 then
   call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
  else
   call SetFloatGameState(GAME_STATE_TIME_OF_DAY,0)
  endif
  set H338U=CreateUnit(Player(0),'hfoo',640,1024,0)
  call SetUnitAcquireRange(H338U,0)
  call H338State()
  if H338Scene!=2 then
   call H338Emit("label=add-Ahid result="+I2S(H338Bool(UnitAddAbility(H338U,'Ahid'))))
  endif
  if H338Scene>=2 then
   call H338Emit("label=add-Ashm result="+I2S(H338Bool(UnitAddAbility(H338U,'Ashm'))))
  endif
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerAddAction(tr,function H338Order)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_SPELL_CAST)
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_SPELL_EFFECT)
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_SPELL_FINISH)
  call TriggerRegisterUnitEvent(tr,H338U,EVENT_UNIT_SPELL_ENDCAST)
  call TriggerAddAction(tr,function H338Spell)
 elseif age==20 then
  call H338Emit("label=ambush result="+I2S(H338Bool(IssueImmediateOrder(H338U,"ambush"))))
 elseif age==30 then
  call H338Emit("label=remove-Ahid result="+I2S(H338Bool(UnitRemoveAbility(H338U,'Ahid'))))
 elseif age==35 then
  call H338Emit("label=move result="+I2S(H338Bool(IssuePointOrder(H338U,"move",896,1024))))
 elseif age==49 then
  call RemoveUnit(H338U)
  call H338Emit("label=cleanup")
 endif
 if age<49 then
  call H338State()
 endif
 set H338Tick=H338Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1338)
 call SetTimeOfDayScale(0)
 call FogEnable(false)
 call FogMaskEnable(false)
 set H338Timer=CreateTimer()
 call TimerStart(H338Timer,0.1,true,function H338Step)
endfunction
