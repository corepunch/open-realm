globals
 unit I324U=null
 unit I324C=null
 item I324Item=null
 timer I324Timer=null
 integer I324Tick=0
 integer I324Scene=0
endglobals
function I324Bool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function I324Emit takes string s returns nothing
 local string v="I324 tick="+I2S(I324Tick)+" scene="+I2S(I324Scene)+" "+s
 if StringLength(v)>160 then
  call Preload("I324 tick="+I2S(I324Tick)+" label=OVERSIZE")
 else
  call Preload(v)
 endif
endfunction
function I324State takes nothing returns nothing
 call I324Emit("label=pose x="+R2S(GetUnitX(I324U))+" y="+R2S(GetUnitY(I324U)))
 call I324Emit("label=state inv="+I2S(GetUnitAbilityLevel(I324U,'Binv'))+" sleep="+I2S(GetUnitAbilityLevel(I324U,'BUsl'))+" guard="+I2S(GetUnitAbilityLevel(I324U,'BUsp'))+" apiv="+I2S(GetUnitAbilityLevel(I324U,'Apiv')))
 call I324Emit("label=visibility invisible="+I2S(I324Bool(IsUnitInvisible(I324U,Player(1))))+" paused="+I2S(I324Bool(IsUnitPaused(I324U)))+" head="+I2S(GetUnitCurrentOrder(I324U)))
endfunction
function I324Spell takes nothing returns nothing
 call I324Emit("label=spell event="+I2S(GetHandleId(GetTriggerEventId()))+" code="+I2S(GetSpellAbilityId()))
endfunction
function I324Step takes nothing returns nothing
 local integer age=ModuloInteger(I324Tick,24)
 local trigger tr
 if I324Tick==0 then
 call I324Emit("label=start")
 call PreloadGenEnd("rs-invisibility324-start.txt")
 call PreloadGenClear()
 call PreloadGenStart()
 call I324Emit("label=start")
 endif
 if age==0 then
  if I324U!=null then
   call RemoveUnit(I324U)
   call RemoveUnit(I324C)
   call RemoveItem(I324Item)
  endif
  set I324Scene=I324Tick/24
  if I324Scene==3 then
   call I324Emit("label=complete")
   call PreloadGenEnd("rs-invisibility324.txt")
   call PauseTimer(I324Timer)
   return
  endif
  set I324U=CreateUnit(Player(0),'Hpal',1024,512,0)
  set I324C=CreateUnit(Player(1),'hfoo',1400,512,180)
  call SetUnitAcquireRange(I324U,51)
  call SetUnitAcquireRange(I324C,51)
  call I324Emit("label=inventory level="+I2S(GetUnitAbilityLevel(I324U,'AInv')))
  call I324Emit("label=ability result="+I2S(I324Bool(UnitAddAbility(I324C,'A324'))))
  set I324Item=CreateItem('pinv',1024,512)
  call I324Emit("label=pickup result="+I2S(I324Bool(UnitAddItem(I324U,I324Item))))
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,I324C,EVENT_UNIT_SPELL_EFFECT)
  call TriggerAddAction(tr,function I324Spell)
 elseif age==1 then
  if I324Scene==2 then
   call I324Emit("label=add-apiv result="+I2S(I324Bool(UnitAddAbility(I324U,'Apiv'))))
  else
   call I324Emit("label=use result="+I2S(I324Bool(UnitUseItem(I324U,I324Item))))
  endif
 elseif age==2 then
  if I324Scene==1 then
   call I324Emit("label=add-apiv result="+I2S(I324Bool(UnitAddAbility(I324U,'Apiv'))))
  elseif I324Scene==2 then
   call I324Emit("label=use result="+I2S(I324Bool(UnitUseItem(I324U,I324Item))))
  endif
 elseif age==3 then
  call I324Emit("label=sleep-order result="+I2S(I324Bool(IssueTargetOrder(I324C,"sleep",I324U))))
 elseif age==5 and I324Scene!=0 then
  call PauseUnit(I324U,true)
  call I324Emit("label=pause")
 elseif age==10 and I324Scene!=0 then
  call UnitRemoveBuffs(I324U,false,true)
  call I324Emit("label=remove-negative")
 elseif age==11 and I324Scene==1 then
  call I324Emit("label=remove-apiv result="+I2S(I324Bool(UnitRemoveAbility(I324U,'Apiv'))))
 elseif age==21 and I324Scene==2 then
  call I324Emit("label=remove-apiv result="+I2S(I324Bool(UnitRemoveAbility(I324U,'Apiv'))))
 elseif age==14 and I324Scene!=0 then
  call PauseUnit(I324U,false)
  call I324Emit("label=unpause")
 endif
 call I324State()
 set I324Tick=I324Tick+1
endfunction
function I324Init takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 set I324Timer=CreateTimer()
 call TimerStart(I324Timer,0.5,true,function I324Step)
endfunction
