globals
 timer udg_S279Timer=null
 integer udg_S279Tick=0
 integer udg_S279Scene=-1
 integer udg_S279Local=0
 unit udg_S279Caster=null
 unit udg_S279Target=null
endglobals
function S279Mark takes string label returns nothing
 call Preload("S279 tick="+I2S(udg_S279Tick)+" scene="+I2S(udg_S279Scene)+" l="+I2S(udg_S279Local)+" label="+label)
endfunction
function S279Sample takes string label returns nothing
 call Preload("S279 tick="+I2S(udg_S279Tick)+" scene="+I2S(udg_S279Scene)+" l="+I2S(udg_S279Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S279Caster))+" x="+R2SW(GetUnitX(udg_S279Caster),1,4)+" y="+R2SW(GetUnitY(udg_S279Caster),1,4)+" mana="+R2SW(GetUnitState(udg_S279Caster,UNIT_STATE_MANA),1,4)+" buff="+I2S(GetUnitAbilityLevel(udg_S279Target,'Binf')))
endfunction
function S279Setup takes nothing returns nothing
 local integer kind='hhou'
 local boolean accepted=false
 call S279Mark("begin-setup")
 if udg_S279Scene==3 then
  set kind='hbar'
 elseif udg_S279Scene==4 then
  set kind='etrp'
 elseif udg_S279Scene==5 then
  set kind='hfoo'
 endif
 set udg_S279Caster=CreateUnit(Player(0),'Hpal',288,288,0)
 set udg_S279Target=CreateUnit(Player(0),kind,1536,288,0)
 call SetUnitState(udg_S279Caster,UNIT_STATE_MANA,200)
 call UnitAddAbility(udg_S279Caster,'A279')
 call S279Sample("created")
 set accepted=IssueTargetOrder(udg_S279Caster,"innerfire",udg_S279Target)
 if accepted then
  call S279Sample("accepted")
 else
  call S279Sample("rejected")
 endif
endfunction
function S279Cleanup takes nothing returns nothing
 call S279Mark("begin-cleanup")
 call RemoveUnit(udg_S279Caster)
 if udg_S279Target!=null then
  call RemoveUnit(udg_S279Target)
 endif
 set udg_S279Caster=null
 set udg_S279Target=null
 call S279Mark("end-cleanup")
endfunction
function S279Tick takes nothing returns nothing
 set udg_S279Tick=udg_S279Tick+1
 if udg_S279Tick==1 then
  call S279Mark("start")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_S279Tick==20 then
  set udg_S279Scene=0
  call S279Setup()
 elseif udg_S279Scene>=0 then
  set udg_S279Local=udg_S279Local+1
  if udg_S279Local==10 then
   if udg_S279Scene==1 then
    call IssueImmediateOrder(udg_S279Caster,"stop")
    call S279Sample("stopped")
   elseif udg_S279Scene==2 then
    call RemoveUnit(udg_S279Target)
    set udg_S279Target=null
    call S279Sample("removed")
   endif
  endif
  call S279Sample("sample")
  if udg_S279Local==100 then
   call S279Cleanup()
   set udg_S279Local=0
   set udg_S279Scene=udg_S279Scene+1
   if udg_S279Scene==6 then
    call S279Mark("complete")
    call PauseTimer(udg_S279Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call S279Setup()
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
 set udg_S279Timer=CreateTimer()
 call TimerStart(udg_S279Timer,0.1,true,function S279Tick)
endfunction
