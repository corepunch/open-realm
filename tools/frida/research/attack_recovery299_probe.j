globals
 unit udg_ARSource=null
 unit udg_ARTarget=null
 unit udg_ARCaster=null
 timer udg_ARTimer=null
 integer udg_ARTick=0
 integer udg_ARScene=0
endglobals
function ARBool takes boolean value returns integer
 if value then
  return 1
 endif
 return 0
endfunction
function AREmit takes string text returns nothing
 local string line="AR299 tick="+I2S(udg_ARTick)+" scene="+I2S(udg_ARScene)+" "+text
 if StringLength(line)>160 then
  call Preload("AR299 tick="+I2S(udg_ARTick)+" scene="+I2S(udg_ARScene)+" label=OVERSIZE")
 else
  call Preload(line)
 endif
endfunction
function ARUnit takes unit u, integer role returns nothing
 call AREmit("label=pose role="+I2S(role)+" x="+R2S(GetUnitX(u))+" y="+R2S(GetUnitY(u))+" angle="+R2S(GetUnitFacing(u)))
 call AREmit("label=unit role="+I2S(role)+" type="+I2S(GetUnitTypeId(u))+" owner="+I2S(GetPlayerId(GetOwningPlayer(u)))+" head="+I2S(GetUnitCurrentOrder(u))+" life="+R2S(GetWidgetLife(u)))
endfunction
function ARMark takes string label returns nothing
 call AREmit("label="+label)
 call ARUnit(udg_ARSource,1)
 call ARUnit(udg_ARTarget,2)
 if udg_ARCaster!=null then
  call ARUnit(udg_ARCaster,3)
 endif
endfunction
function AROrder takes nothing returns nothing
 local unit u=GetTriggerUnit()
 local integer role=0
 if u==udg_ARSource then
  set role=1
 elseif u==udg_ARTarget then
  set role=2
 elseif u==udg_ARCaster then
  set role=3
 endif
 call AREmit("label=issued role="+I2S(role)+" order="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(u)))
endfunction
function ARSpell takes nothing returns nothing
 local integer role=0
 if GetTriggerUnit()==udg_ARCaster then
  set role=3
 elseif GetTriggerUnit()==udg_ARSource then
  set role=1
 endif
 call AREmit("label=spell role="+I2S(role)+" event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId()))
endfunction
function ARWatch takes unit u returns nothing
 local trigger tr=CreateTrigger()
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_ISSUED_ORDER)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_ISSUED_POINT_ORDER)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_ISSUED_TARGET_ORDER)
 call TriggerAddAction(tr,function AROrder)
 set tr=CreateTrigger()
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_SPELL_CHANNEL)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_SPELL_CAST)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_SPELL_EFFECT)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_SPELL_FINISH)
 call TriggerRegisterUnitEvent(tr,u,EVENT_UNIT_SPELL_ENDCAST)
 call TriggerAddAction(tr,function ARSpell)
endfunction
function ARTick takes nothing returns nothing
 local integer t=ModuloInteger(udg_ARTick,300)
 local boolean accepted=false
 if t==0 then
  set udg_ARSource=CreateUnit(Player(0),'hfoo',1344.0,288.0,0.0)
  set udg_ARTarget=CreateUnit(Player(1),'hfoo',1456.0,288.0,90.0)
  call SetUnitMoveSpeed(udg_ARSource,150.0)
  call SetUnitMoveSpeed(udg_ARTarget,270.0)
  call SetUnitAcquireRange(udg_ARSource,600.0)
  call ARWatch(udg_ARSource)
  call ARWatch(udg_ARTarget)
  call AREmit("label=source_setup acquire="+R2S(GetUnitAcquireRange(udg_ARSource))+" attack="+I2S(GetUnitAbilityLevel(udg_ARSource,'Aatk')))
  call ARMark("begin")
 elseif t==1 and udg_ARScene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if udg_ARScene!=3 then
   set accepted=IssueTargetOrder(udg_ARSource,"attack",udg_ARTarget)
   call AREmit("label=attack_return accepted="+I2S(ARBool(accepted)))
  endif
  set accepted=IssuePointOrder(udg_ARTarget,"move",1456.0,1856.0)
  call AREmit("label=move_return accepted="+I2S(ARBool(accepted)))
 elseif t==55 and udg_ARScene==4 then
  set udg_ARCaster=CreateUnit(Player(0),'hsor',GetUnitX(udg_ARSource)-100.0,GetUnitY(udg_ARSource),0.0)
  call SetUnitState(udg_ARCaster,UNIT_STATE_MANA,500.0)
  call ARWatch(udg_ARCaster)
  call AREmit("label=invis_setup level="+I2S(GetUnitAbilityLevel(udg_ARCaster,'Aivs'))+" research="+I2S(GetPlayerTechCount(Player(0),'Rhst',true)))
  call AREmit("label=caster_mana current="+R2S(GetUnitState(udg_ARCaster,UNIT_STATE_MANA))+" maximum="+R2S(GetUnitState(udg_ARCaster,UNIT_STATE_MAX_MANA)))
  call ARMark("caster_ready")
 elseif t==60 then
  call ARMark("before_intervention")
  if udg_ARScene==0 or udg_ARScene==3 then
   call ShowUnit(udg_ARTarget,false)
  elseif udg_ARScene==1 then
   call SetUnitOwner(udg_ARTarget,Player(0),false)
  elseif udg_ARScene==2 then
   set accepted=UnitRemoveAbility(udg_ARSource,'Aatk')
   call AREmit("label=remove_attack_return accepted="+I2S(ARBool(accepted)))
  elseif udg_ARScene==4 then
   set accepted=IssueTargetOrder(udg_ARCaster,"invisibility",udg_ARSource)
   call AREmit("label=invisibility_return accepted="+I2S(ARBool(accepted)))
  elseif udg_ARScene==5 then
   call PauseUnit(udg_ARSource,true)
   call ARMark("paused")
   call ShowUnit(udg_ARTarget,false)
  endif
  call ARMark("after_intervention")
 elseif t==90 and udg_ARScene==5 then
  call ARMark("before_resume")
  call PauseUnit(udg_ARSource,false)
  call ARMark("after_resume")
 elseif t==260 then
  call ARMark("settled")
 elseif t==280 then
  call IssueImmediateOrder(udg_ARSource,"stop")
  call IssueImmediateOrder(udg_ARTarget,"stop")
 elseif t==290 then
  call RemoveUnit(udg_ARSource)
  call RemoveUnit(udg_ARTarget)
  if udg_ARCaster!=null then
   call RemoveUnit(udg_ARCaster)
   set udg_ARCaster=null
  endif
 elseif t==299 then
  if udg_ARScene==5 then
   call AREmit("label=complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_ARTimer)
  endif
  set udg_ARScene=udg_ARScene+1
 endif
 if t<290 and ModuloInteger(t,5)==0 then
  call ARMark("sample")
 endif
 set udg_ARTick=udg_ARTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetPlayerTechResearched(Player(0),'Rhst',2)
 call SetTimeOfDayScale(0.0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12.0)
 call AREmit("label=start")
 set udg_ARTimer=CreateTimer()
 call TimerStart(udg_ARTimer,0.1,true,function ARTick)
endfunction
