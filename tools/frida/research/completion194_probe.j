globals
 timer udg_C194Timer=null
 integer udg_C194Tick=0
 unit udg_C194Caster=null
 unit udg_C194Target=null
 unit udg_C194Peer=null
endglobals
function C194Mark takes string label returns nothing
 call Preload("C194 tick="+I2S(udg_C194Tick)+" label="+label)
endfunction
function C194Channel takes nothing returns nothing
 call C194Mark("channel-before-mutation")
 call RemoveUnit(udg_C194Peer)
 call IssuePointOrder(udg_C194Caster,"move",288,1536)
 call C194Mark("channel-after-mutation")
endfunction
function C194Cast takes nothing returns nothing
 call C194Mark("spell-cast")
endfunction
function C194Effect takes nothing returns nothing
 call C194Mark("effect-before-mutation")
 call C194Mark("effect-after-mutation")
endfunction
function C194Tick takes nothing returns nothing
 local trigger tr=null
 set udg_C194Tick=udg_C194Tick+1
 if udg_C194Tick==20 then
  call C194Mark("begin-setup")
  set udg_C194Caster=CreateUnit(Player(0),'Hpal',288,288,0)
  set udg_C194Target=CreateUnit(Player(0),'hfoo',1536,288,0)
  set udg_C194Peer=CreateUnit(Player(0),'hfoo',288,1800,0)
  call SelectHeroSkill(udg_C194Caster,'AHhb')
  call SetUnitState(udg_C194Target,UNIT_STATE_LIFE,200)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_C194Caster,EVENT_UNIT_SPELL_CHANNEL)
  call TriggerAddAction(tr,function C194Channel)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_C194Caster,EVENT_UNIT_SPELL_CAST)
  call TriggerAddAction(tr,function C194Cast)
  set tr=CreateTrigger()
  call TriggerRegisterUnitEvent(tr,udg_C194Caster,EVENT_UNIT_SPELL_EFFECT)
  call TriggerAddAction(tr,function C194Effect)
  call IssuePointOrder(udg_C194Peer,"move",1800,1800)
  if IssueTargetOrder(udg_C194Caster,"holybolt",udg_C194Target) then
   call C194Mark("accepted")
  else
   call C194Mark("rejected")
  endif
 endif
 if udg_C194Tick>=20 then
  call Preload("C194 tick="+I2S(udg_C194Tick)+" label=sample order="+I2S(GetUnitCurrentOrder(udg_C194Caster))+" x="+R2SW(GetUnitX(udg_C194Caster),1,4)+" y="+R2SW(GetUnitY(udg_C194Caster),1,4)+" life="+R2SW(GetUnitState(udg_C194Target,UNIT_STATE_LIFE),1,4))
 endif
 if udg_C194Tick==140 then
  call C194Mark("complete")
  call PauseTimer(udg_C194Timer)
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
 set udg_C194Timer=CreateTimer()
 call TimerStart(udg_C194Timer,0.1,true,function C194Tick)
endfunction
