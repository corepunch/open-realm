globals
 timer udg_C196Timer=null
 integer udg_C196Tick=0
 integer udg_C196Scene=-1
 integer udg_C196Local=0
 unit udg_C196Caster=null
 unit udg_C196Target=null
endglobals
function C196Mark takes string label returns nothing
 call Preload("C196 tick="+I2S(udg_C196Tick)+" s="+I2S(udg_C196Scene)+" l="+I2S(udg_C196Local)+" label="+label)
endfunction
function C196Sample takes string label returns nothing
 call Preload("C196 tick="+I2S(udg_C196Tick)+" s="+I2S(udg_C196Scene)+" l="+I2S(udg_C196Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_C196Caster))+" x="+R2SW(GetUnitX(udg_C196Caster),1,4)+" y="+R2SW(GetUnitY(udg_C196Caster),1,4)+" tx="+R2SW(GetUnitX(udg_C196Target),1,4)+" ty="+R2SW(GetUnitY(udg_C196Target),1,4)+" life="+R2SW(GetUnitState(udg_C196Target,UNIT_STATE_LIFE),1,4)+" mana="+R2SW(GetUnitState(udg_C196Caster,UNIT_STATE_MANA),1,4))
endfunction
function C196Setup takes nothing returns nothing
 local boolean accepted=false
 call C196Mark("begin-setup")
 set udg_C196Caster=CreateUnit(Player(0),'Hpal',288,288,0)
 set udg_C196Target=CreateUnit(Player(0),'hfoo',1856,288,0)
 call SelectHeroSkill(udg_C196Caster,'AHhb')
 call SetUnitState(udg_C196Target,UNIT_STATE_LIFE,200)
 call C196Sample("created")
 if udg_C196Scene==1 then
  set accepted=IssueTargetOrder(udg_C196Caster,"move",udg_C196Target)
 else
  set accepted=IssueTargetOrder(udg_C196Caster,"holybolt",udg_C196Target)
 endif
 if accepted then
  call C196Sample("accepted")
 else
  call C196Sample("rejected")
 endif
 if udg_C196Scene==2 then
  call IssuePointOrder(udg_C196Target,"move",1536,896)
 endif
endfunction
function C196Cleanup takes nothing returns nothing
 call C196Mark("begin-cleanup")
 call RemoveUnit(udg_C196Caster)
 call RemoveUnit(udg_C196Target)
 set udg_C196Caster=null
 set udg_C196Target=null
 call C196Mark("end-cleanup")
endfunction
function C196Tick takes nothing returns nothing
 set udg_C196Tick=udg_C196Tick+1
 if udg_C196Tick==20 then
  set udg_C196Scene=0
  call C196Setup()
 elseif udg_C196Scene>=0 then
  set udg_C196Local=udg_C196Local+1
  call C196Sample("sample")
  if udg_C196Local==180 then
   call C196Cleanup()
   set udg_C196Local=0
   set udg_C196Scene=udg_C196Scene+1
   if udg_C196Scene==1 then
    call C196Mark("complete")
    call PauseTimer(udg_C196Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call C196Setup()
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
 set udg_C196Timer=CreateTimer()
 call TimerStart(udg_C196Timer,0.1,true,function C196Tick)
endfunction
