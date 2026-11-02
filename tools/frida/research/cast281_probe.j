globals
 timer udg_C281Timer=null
 integer udg_C281Tick=0
 integer udg_C281Scene=-1
 integer udg_C281Local=0
 integer udg_C281Mode=0
 integer udg_C281Channels=0
 integer udg_C281Units=0
 integer udg_C281Effects=0
 unit udg_C281Caster=null
 trigger udg_C281Player=null
 trigger udg_C281Unit=null
endglobals
function C281Mark takes string label returns nothing
 call Preload("C281 tick="+I2S(udg_C281Tick)+" scene="+I2S(udg_C281Scene)+" l="+I2S(udg_C281Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_C281Caster))+" x="+R2SW(GetUnitX(udg_C281Caster),1,4)+" y="+R2SW(GetUnitY(udg_C281Caster),1,4)+" mana="+R2SW(GetUnitState(udg_C281Caster,UNIT_STATE_MANA),1,4)+" channels="+I2S(udg_C281Channels)+" units="+I2S(udg_C281Units)+" effects="+I2S(udg_C281Effects))
endfunction
function C281PlayerChannel takes nothing returns nothing
 set udg_C281Channels=udg_C281Channels+1
 call C281Mark("channel-before")
 call Preload("C281 tick="+I2S(udg_C281Tick)+" scene="+I2S(udg_C281Scene)+" l="+I2S(udg_C281Local)+" label=context code="+I2S(GetSpellAbilityId())+" tx="+R2SW(GetSpellTargetX(),1,4)+" ty="+R2SW(GetSpellTargetY(),1,4)+" target="+I2S(GetHandleId(GetSpellTargetUnit()))+" caster="+I2S(GetHandleId(GetSpellAbilityUnit()))+" trigger="+I2S(GetHandleId(GetTriggerUnit())))
 if udg_C281Mode==1 then
  call IssueImmediateOrder(udg_C281Caster,"stop")
 elseif udg_C281Mode==2 then
  call IssuePointOrder(udg_C281Caster,"move",288,1536)
 elseif udg_C281Mode==3 then
  call RemoveUnit(udg_C281Caster)
 endif
 call C281Mark("channel-after")
endfunction
function C281UnitChannel takes nothing returns nothing
 set udg_C281Units=udg_C281Units+1
 call C281Mark("unit-channel")
endfunction
function C281Effect takes nothing returns nothing
 set udg_C281Effects=udg_C281Effects+1
 call C281Mark("effect")
endfunction
function C281Setup takes nothing returns nothing
 local integer kind='Hblm'
 local boolean accepted=false
 set udg_C281Mode=udg_C281Scene-(udg_C281Scene/4)*4
 set udg_C281Channels=0
 set udg_C281Units=0
 set udg_C281Effects=0
 call C281Mark("begin-setup")
 if udg_C281Scene>=4 then
  set kind='edtm'
 endif
 set udg_C281Caster=CreateUnit(Player(0),kind,288,288,0)
 call SetUnitState(udg_C281Caster,UNIT_STATE_MANA,200)
 call UnitAddAbility(udg_C281Caster,'A281')
 set udg_C281Unit=CreateTrigger()
 call TriggerRegisterUnitEvent(udg_C281Unit,udg_C281Caster,EVENT_UNIT_SPELL_CHANNEL)
 call TriggerAddAction(udg_C281Unit,function C281UnitChannel)
 call C281Mark("created")
 call IssuePointOrder(udg_C281Caster,"move",288,896)
 set accepted=IssuePointOrder(udg_C281Caster,"flamestrike",1536,288)
 if accepted then
  call C281Mark("accepted")
 else
  call C281Mark("rejected")
 endif
endfunction
function C281Cleanup takes nothing returns nothing
 call C281Mark("begin-cleanup")
 call DestroyTrigger(udg_C281Unit)
 call RemoveUnit(udg_C281Caster)
 set udg_C281Caster=null
 call C281Mark("end-cleanup")
endfunction
function C281Tick takes nothing returns nothing
 set udg_C281Tick=udg_C281Tick+1
 if udg_C281Tick==1 then
  call C281Mark("start")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_C281Tick==20 then
  set udg_C281Scene=0
  call C281Setup()
 elseif udg_C281Scene>=0 then
  set udg_C281Local=udg_C281Local+1
  call C281Mark("sample")
  if udg_C281Local==80 then
   call C281Cleanup()
   set udg_C281Local=0
   set udg_C281Scene=udg_C281Scene+1
   if udg_C281Scene==8 then
    call C281Mark("complete")
    call PauseTimer(udg_C281Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call C281Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger effectTrigger=CreateTrigger()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_C281Player=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(udg_C281Player,Player(0),EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)
 call TriggerAddAction(udg_C281Player,function C281PlayerChannel)
 call TriggerRegisterPlayerUnitEvent(effectTrigger,Player(0),EVENT_PLAYER_UNIT_SPELL_EFFECT,null)
 call TriggerAddAction(effectTrigger,function C281Effect)
 set udg_C281Timer=CreateTimer()
 call TimerStart(udg_C281Timer,0.1,true,function C281Tick)
endfunction
