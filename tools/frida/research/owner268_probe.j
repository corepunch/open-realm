globals
 timer udg_O268Timer=null
 integer udg_O268Tick=0
 integer udg_O268Scene=-1
 integer udg_O268Local=0
 unit udg_O268Caster=null
 unit udg_O268Target=null
endglobals
function O268Mark takes string label returns nothing
 call Preload("O268 tick="+I2S(udg_O268Tick)+" s="+I2S(udg_O268Scene)+" l="+I2S(udg_O268Local)+" label="+label)
endfunction
function O268Sample takes string label returns nothing
 call Preload("O268 tick="+I2S(udg_O268Tick)+" s="+I2S(udg_O268Scene)+" l="+I2S(udg_O268Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_O268Caster))+" x="+R2SW(GetUnitX(udg_O268Caster),1,4)+" y="+R2SW(GetUnitY(udg_O268Caster),1,4)+" life="+R2SW(GetUnitState(udg_O268Target,UNIT_STATE_LIFE),1,4)+" mana="+R2SW(GetUnitState(udg_O268Caster,UNIT_STATE_MANA),1,4))
endfunction
function O268Setup takes nothing returns nothing
 local boolean accepted=false
 call O268Mark("begin-setup")
 if udg_O268Scene<2 then
  set udg_O268Caster=CreateUnit(Player(0),'Hblm',288,288,0)
  set udg_O268Target=CreateUnit(Player(1),'hfoo',1536,288,0)
  call PauseUnit(udg_O268Target,true)
  call SelectHeroSkill(udg_O268Caster,'AHfs')
 else
  set udg_O268Caster=CreateUnit(Player(0),'edtm',288,288,0)
  set udg_O268Target=CreateUnit(Player(0),'hfoo',1536,288,0)
  call SetUnitState(udg_O268Target,UNIT_STATE_LIFE,200)
  call UnitAddAbility(udg_O268Caster,'AHhb')
  call SetUnitAbilityLevel(udg_O268Caster,'AHhb',1)
  call SetUnitState(udg_O268Caster,UNIT_STATE_MANA,200)
 endif
 call O268Sample("created")
 call IssuePointOrder(udg_O268Caster,"move",288,896)
 call O268Sample("old-move")
 if udg_O268Scene<2 then
  set accepted=IssuePointOrder(udg_O268Caster,"flamestrike",1536,288)
 else
  set accepted=IssueTargetOrder(udg_O268Caster,"holybolt",udg_O268Target)
 endif
 if accepted then
  call O268Sample("accepted")
 else
  call O268Sample("rejected")
 endif
endfunction
function O268Cleanup takes nothing returns nothing
 call O268Mark("begin-cleanup")
 call RemoveUnit(udg_O268Caster)
 call RemoveUnit(udg_O268Target)
 set udg_O268Caster=null
 set udg_O268Target=null
 call O268Mark("end-cleanup")
endfunction
function O268Tick takes nothing returns nothing
 set udg_O268Tick=udg_O268Tick+1
 if udg_O268Tick==20 then
  set udg_O268Scene=0
  call O268Setup()
 elseif udg_O268Scene>=0 then
  set udg_O268Local=udg_O268Local+1
  if udg_O268Local==10 and (udg_O268Scene==1 or udg_O268Scene==3) then
   call IssueImmediateOrder(udg_O268Caster,"stop")
   call O268Sample("stopped")
  endif
  call O268Sample("sample")
  if udg_O268Local==120 then
   call O268Cleanup()
   set udg_O268Local=0
   set udg_O268Scene=udg_O268Scene+1
   if udg_O268Scene==4 then
    call O268Mark("complete")
    call PauseTimer(udg_O268Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call O268Setup()
   endif
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_O268Timer=CreateTimer()
 call TimerStart(udg_O268Timer,0.1,true,function O268Tick)
endfunction
