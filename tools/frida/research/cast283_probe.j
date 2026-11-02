globals
 timer udg_C282Timer=null
 timer udg_C282Cancel=null
 integer udg_C282Tick=0
 integer udg_C282Scene=-1
 integer udg_C282Local=0
 unit udg_C282Caster=null
 unit udg_C282Target=null
 trigger udg_C282Unit=null
endglobals
function C282Mark takes string label returns nothing
 call Preload("C282 tick="+I2S(udg_C282Tick)+" scene="+I2S(udg_C282Scene)+" l="+I2S(udg_C282Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_C282Caster))+" x="+R2SW(GetUnitX(udg_C282Caster),1,4)+" y="+R2SW(GetUnitY(udg_C282Caster),1,4)+" mana="+R2SW(GetUnitState(udg_C282Caster,UNIT_STATE_MANA),1,4))
endfunction
function C282Cancel takes nothing returns nothing
 call C282Mark("windup-before")
 if udg_C282Scene==8 then
  call IssueImmediateOrder(udg_C282Caster,"stop")
 elseif udg_C282Scene==9 then
  call RemoveUnit(udg_C282Caster)
 else
  call KillUnit(udg_C282Target)
 endif
 call C282Mark("windup-after")
endfunction
function C282Event takes nothing returns nothing
 local eventid e=GetTriggerEventId()
 local string phase="unknown"
 local boolean isPlayer=false
 if e==EVENT_PLAYER_UNIT_SPELL_CHANNEL or e==EVENT_UNIT_SPELL_CHANNEL then
  set phase="channel"
  set isPlayer=e==EVENT_PLAYER_UNIT_SPELL_CHANNEL
 elseif e==EVENT_PLAYER_UNIT_SPELL_CAST or e==EVENT_UNIT_SPELL_CAST then
  set phase="cast"
  set isPlayer=e==EVENT_PLAYER_UNIT_SPELL_CAST
 elseif e==EVENT_PLAYER_UNIT_SPELL_EFFECT or e==EVENT_UNIT_SPELL_EFFECT then
  set phase="effect"
  set isPlayer=e==EVENT_PLAYER_UNIT_SPELL_EFFECT
 elseif e==EVENT_PLAYER_UNIT_SPELL_FINISH or e==EVENT_UNIT_SPELL_FINISH then
  set phase="finish"
  set isPlayer=e==EVENT_PLAYER_UNIT_SPELL_FINISH
 elseif e==EVENT_PLAYER_UNIT_SPELL_ENDCAST or e==EVENT_UNIT_SPELL_ENDCAST then
  set phase="endcast"
  set isPlayer=e==EVENT_PLAYER_UNIT_SPELL_ENDCAST
 endif
 if isPlayer then
  set phase="player-"+phase
 else
  set phase="unit-"+phase
 endif
 call C282Mark(phase+"-before")
 call Preload("C282 tick="+I2S(udg_C282Tick)+" scene="+I2S(udg_C282Scene)+" l="+I2S(udg_C282Local)+" label=context phase="+phase+" code="+I2S(GetSpellAbilityId())+" tx="+R2SW(GetSpellTargetX(),1,4)+" ty="+R2SW(GetSpellTargetY(),1,4)+" target="+I2S(GetHandleId(GetSpellTargetUnit()))+" caster="+I2S(GetHandleId(GetSpellAbilityUnit()))+" trigger="+I2S(GetHandleId(GetTriggerUnit())))
 if phase=="player-channel" and (udg_C282Scene==8 or udg_C282Scene==9) then
  call TimerStart(udg_C282Cancel,0.12,false,function C282Cancel)
 elseif phase=="player-channel" and (udg_C282Scene==0 or udg_C282Scene==3) then
  call IssueImmediateOrder(udg_C282Caster,"stop")
 elseif phase=="player-channel" and udg_C282Scene==1 then
  call IssuePointOrder(udg_C282Caster,"move",288,1536)
 elseif phase=="player-channel" and udg_C282Scene==2 then
  call RemoveUnit(udg_C282Caster)
 elseif phase=="player-cast" and (udg_C282Scene==4 or udg_C282Scene==5) then
  call IssueImmediateOrder(udg_C282Caster,"stop")
 elseif phase=="player-effect" and (udg_C282Scene==6 or udg_C282Scene==7) then
  call IssuePointOrder(udg_C282Caster,"move",288,1536)
 elseif phase=="player-effect" and (udg_C282Scene==10 or udg_C282Scene==11) then
  call RemoveUnit(udg_C282Caster)
 endif
 call C282Mark(phase+"-after")
endfunction
function C282Setup takes nothing returns nothing
 local integer abilityCode='A282'
 local boolean unitTarget=udg_C282Scene==3 or udg_C282Scene==5 or udg_C282Scene==7 or udg_C282Scene==11
 local boolean accepted=false
 local integer i=0
 call C282Mark("begin-setup")
 set udg_C282Caster=CreateUnit(Player(0),'Hblm',288,288,0)
 set udg_C282Target=CreateUnit(Player(0),'hfoo',384,288,0)
 call SetUnitState(udg_C282Caster,UNIT_STATE_MANA,200)
 if unitTarget then
  set abilityCode='A283'
 endif
 if udg_C282Scene==2 then
  set abilityCode='A284'
 elseif udg_C282Scene==3 then
  set abilityCode='A285'
 endif
 call UnitAddAbility(udg_C282Caster,abilityCode)
 set udg_C282Unit=CreateTrigger()
 loop
  exitwhen i==5
  call TriggerRegisterUnitEvent(udg_C282Unit,udg_C282Caster,ConvertUnitEvent(289+i))
  set i=i+1
 endloop
 call TriggerAddAction(udg_C282Unit,function C282Event)
 call C282Mark("created")
 if unitTarget then
  set accepted=IssueTargetOrder(udg_C282Caster,"innerfire",udg_C282Target)
 elseif udg_C282Scene==1 then
  set accepted=IssuePointOrder(udg_C282Caster,"flamestrike",1536,288)
 else
  set accepted=IssuePointOrder(udg_C282Caster,"flamestrike",384,288)
 endif
 if accepted then
  call C282Mark("accepted")
 else
  call C282Mark("rejected")
 endif
endfunction
function C282Cleanup takes nothing returns nothing
 call C282Mark("begin-cleanup")
 call DestroyTrigger(udg_C282Unit)
 call RemoveUnit(udg_C282Caster)
 call RemoveUnit(udg_C282Target)
 set udg_C282Caster=null
 set udg_C282Target=null
 call C282Mark("end-cleanup")
endfunction
function C282Tick takes nothing returns nothing
 set udg_C282Tick=udg_C282Tick+1
 if udg_C282Tick==1 then
  call C282Mark("start")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_C282Tick==20 then
  set udg_C282Scene=0
  call C282Setup()
 elseif udg_C282Scene>=0 then
  set udg_C282Local=udg_C282Local+1
  call C282Mark("sample")
  if udg_C282Local==60 then
   call C282Cleanup()
   set udg_C282Local=0
   set udg_C282Scene=udg_C282Scene+1
   if udg_C282Scene==12 then
    call C282Mark("complete")
    call PauseTimer(udg_C282Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call C282Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 loop
  exitwhen i==5
  call TriggerRegisterPlayerUnitEvent(t,Player(0),ConvertPlayerUnitEvent(272+i),null)
  set i=i+1
 endloop
 call TriggerAddAction(t,function C282Event)
 set udg_C282Cancel=CreateTimer()
 set udg_C282Timer=CreateTimer()
 call TimerStart(udg_C282Timer,0.1,true,function C282Tick)
endfunction
