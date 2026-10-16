globals
 timer udg_S184Timer=null
 integer udg_S184Tick=0
 integer udg_S184Scene=-1
 integer udg_S184Local=0
 unit udg_S184Caster=null
 unit udg_S184Target=null
endglobals
function S184Mark takes string label returns nothing
 call Preload("S184 tick="+I2S(udg_S184Tick)+" s="+I2S(udg_S184Scene)+" l="+I2S(udg_S184Local)+" label="+label)
endfunction
function S184Sample takes string label returns nothing
 call Preload("S184 tick="+I2S(udg_S184Tick)+" s="+I2S(udg_S184Scene)+" l="+I2S(udg_S184Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S184Caster))+" x="+R2SW(GetUnitX(udg_S184Caster),1,4)+" y="+R2SW(GetUnitY(udg_S184Caster),1,4)+" tx="+R2SW(GetUnitX(udg_S184Target),1,4)+" ty="+R2SW(GetUnitY(udg_S184Target),1,4)+" life="+R2SW(GetUnitState(udg_S184Target,UNIT_STATE_LIFE),1,4)+" mana="+R2SW(GetUnitState(udg_S184Caster,UNIT_STATE_MANA),1,4))
endfunction
function S184Setup takes nothing returns nothing
 local boolean accepted=false
 call S184Mark("begin-setup")
 set udg_S184Caster=CreateUnit(Player(0),'Hpal',288,288,0)
 set udg_S184Target=CreateUnit(Player(0),'hfoo',1536,288,0)
 call SelectHeroSkill(udg_S184Caster,'AHhb')
 call SetUnitState(udg_S184Target,UNIT_STATE_LIFE,200)
 call S184Sample("created")
 if udg_S184Scene==1 then
  set accepted=IssueTargetOrder(udg_S184Caster,"move",udg_S184Target)
 else
  set accepted=IssueTargetOrder(udg_S184Caster,"holybolt",udg_S184Target)
 endif
 if accepted then
  call S184Sample("accepted")
 else
  call S184Sample("rejected")
 endif
 if udg_S184Scene==2 then
  call IssuePointOrder(udg_S184Target,"move",1536,896)
 endif
endfunction
function S184Cleanup takes nothing returns nothing
 call S184Mark("begin-cleanup")
 call RemoveUnit(udg_S184Caster)
 call RemoveUnit(udg_S184Target)
 set udg_S184Caster=null
 set udg_S184Target=null
 call S184Mark("end-cleanup")
endfunction
function S184Tick takes nothing returns nothing
 set udg_S184Tick=udg_S184Tick+1
 if udg_S184Tick==20 then
  set udg_S184Scene=0
  call S184Setup()
 elseif udg_S184Scene>=0 then
  set udg_S184Local=udg_S184Local+1
  call S184Sample("sample")
  if udg_S184Local==120 then
   call S184Cleanup()
   set udg_S184Local=0
   set udg_S184Scene=udg_S184Scene+1
   if udg_S184Scene==3 then
    call S184Mark("complete")
    call PauseTimer(udg_S184Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call S184Setup()
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
 set udg_S184Timer=CreateTimer()
 call TimerStart(udg_S184Timer,0.1,true,function S184Tick)
endfunction
