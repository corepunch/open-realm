globals
 timer udg_I209Timer=null
 integer udg_I209Tick=0
 unit udg_I209Caster=null
 unit udg_I209Target=null
 unit array udg_I209Peer
 integer udg_I209Channel=0
 integer udg_I209Cast=0
 integer udg_I209Effect=0
 integer udg_I209Orders=0
 boolean udg_I209Resumed=false
endglobals
function I209Mark takes string label returns nothing
 call Preload("I209 tick="+I2S(udg_I209Tick)+" label="+label)
endfunction
function I209Order takes nothing returns nothing
 set udg_I209Orders=udg_I209Orders+1
 call I209Mark("issued order="+I2S(GetIssuedOrderId()))
 if GetIssuedOrderId()==OrderId("undefend") and not udg_I209Resumed then
  set udg_I209Resumed=true
  call I209Mark("release-before-mutation")
  call IssuePointOrder(udg_I209Caster,"move",1536,1536)
  call I209Mark("release-after-mutation")
 endif
endfunction
function I209Channel takes nothing returns nothing
 local integer i=0
 local boolean accepted=false
 set udg_I209Channel=udg_I209Channel+1
 call I209Mark("channel-before-mutation")
 loop
  exitwhen i==4
  call I209Mark("remove-before role="+I2S(i))
  call RemoveUnit(udg_I209Peer[i])
  call I209Mark("remove-after role="+I2S(i))
  if i==0 then
   set accepted=IssueImmediateOrder(udg_I209Peer[i],"stop")
  elseif i==1 then
   set accepted=IssueImmediateOrder(udg_I209Peer[i],"holdposition")
  elseif i==2 then
   set accepted=IssueTargetOrder(udg_I209Peer[i],"move",udg_I209Target)
  else
   set accepted=IssuePointOrder(udg_I209Peer[i],"move",288,1800)
  endif
  if accepted then
   call I209Mark("accepted role="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_I209Peer[i])))
  else
   call I209Mark("rejected role="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_I209Peer[i])))
  endif
  set i=i+1
 endloop
 call IssuePointOrder(udg_I209Caster,"move",288,1536)
 call I209Mark("channel-after-mutation")
endfunction
function I209Cast takes nothing returns nothing
 set udg_I209Cast=udg_I209Cast+1
 call I209Mark("cast")
endfunction
function I209Effect takes nothing returns nothing
 set udg_I209Effect=udg_I209Effect+1
 call I209Mark("effect")
endfunction
function I209Tick takes nothing returns nothing
 local trigger tr=null
 local integer i=0
 set udg_I209Tick=udg_I209Tick+1
 if udg_I209Tick==20 then
  call I209Mark("begin-setup")
  set udg_I209Caster=CreateUnit(Player(0),'Hpal',288,288,0)
  set udg_I209Target=CreateUnit(Player(0),'hfoo',1536,288,0)
  call SelectHeroSkill(udg_I209Caster,'AHhb')
  call SetUnitState(udg_I209Target,UNIT_STATE_LIFE,200)
  loop
   exitwhen i==4
   set udg_I209Peer[i]=CreateUnit(Player(0),'hfoo',288,900+200*i,0)
   set tr=CreateTrigger()
   call TriggerRegisterUnitEvent(tr,udg_I209Peer[i],EVENT_UNIT_ISSUED_ORDER)
   call TriggerRegisterUnitEvent(tr,udg_I209Peer[i],EVENT_UNIT_ISSUED_POINT_ORDER)
   call TriggerRegisterUnitEvent(tr,udg_I209Peer[i],EVENT_UNIT_ISSUED_TARGET_ORDER)
   call TriggerAddAction(tr,function I209Order)
   call IssuePointOrder(udg_I209Peer[i],"move",1800,900+200*i)
   set i=i+1
  endloop
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_I209Caster,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerAddAction(tr,function I209Channel)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_I209Caster,EVENT_UNIT_SPELL_CAST)
  call TriggerAddAction(tr,function I209Cast)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_I209Caster,EVENT_UNIT_SPELL_EFFECT)
  call TriggerAddAction(tr,function I209Effect)
  if IssueTargetOrder(udg_I209Caster,"holybolt",udg_I209Target) then
   call I209Mark("spell-accepted")
  else
   call I209Mark("spell-rejected")
  endif
 endif
 if udg_I209Tick>=20 then
  call I209Mark("sample order="+I2S(GetUnitCurrentOrder(udg_I209Caster))+" x="+R2SW(GetUnitX(udg_I209Caster),1,4)+" y="+R2SW(GetUnitY(udg_I209Caster),1,4)+" life="+R2SW(GetUnitState(udg_I209Target,UNIT_STATE_LIFE),1,4)+" channel="+I2S(udg_I209Channel)+" cast="+I2S(udg_I209Cast)+" effect="+I2S(udg_I209Effect)+" orders="+I2S(udg_I209Orders))
 endif
 if udg_I209Tick==140 then
  call I209Mark("complete")
  call PauseTimer(udg_I209Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_I209Timer=CreateTimer()
 call TimerStart(udg_I209Timer,0.1,true,function I209Tick)
endfunction
