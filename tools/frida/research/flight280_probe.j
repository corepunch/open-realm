globals
 timer udg_F280Timer=null
 integer udg_F280Tick=0
 integer udg_F280Scene=-1
 integer udg_F280Local=0
 unit udg_F280Caster=null
 unit udg_F280Target=null
endglobals
function F280Mark takes string label returns nothing
 call Preload("F280 tick="+I2S(udg_F280Tick)+" scene="+I2S(udg_F280Scene)+" l="+I2S(udg_F280Local)+" label="+label)
endfunction
function F280Sample takes string label returns nothing
 call Preload("F280 tick="+I2S(udg_F280Tick)+" scene="+I2S(udg_F280Scene)+" l="+I2S(udg_F280Local)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_F280Caster))+" x="+R2SW(GetUnitX(udg_F280Caster),1,4)+" y="+R2SW(GetUnitY(udg_F280Caster),1,4)+" mana="+R2SW(GetUnitState(udg_F280Caster,UNIT_STATE_MANA),1,4)+" buff="+I2S(GetUnitAbilityLevel(udg_F280Target,'Binf')))
endfunction
function F280Setup takes nothing returns nothing
 local integer kind='hfoo'
 local boolean accepted=false
 call F280Mark("begin-setup")
 if udg_F280Scene==5 then
  set kind='hhou'
 endif
 set udg_F280Caster=CreateUnit(Player(0),'edtm',288,288,0)
 set udg_F280Target=CreateUnit(Player(0),kind,1536,288,0)
 call SetUnitState(udg_F280Caster,UNIT_STATE_MANA,200)
 call UnitAddAbility(udg_F280Caster,'A280')
 call F280Sample("created")
 call IssuePointOrder(udg_F280Caster,"move",288,896)
 if udg_F280Scene==3 or udg_F280Scene==4 then
  call UnitAddAbility(udg_F280Caster,'A281')
  set accepted=IssuePointOrder(udg_F280Caster,"flamestrike",1536,288)
 else
  set accepted=IssueTargetOrder(udg_F280Caster,"innerfire",udg_F280Target)
 endif
 if accepted then
  call F280Sample("accepted")
 else
  call F280Sample("rejected")
 endif
endfunction
function F280Cleanup takes nothing returns nothing
 call F280Mark("begin-cleanup")
 call RemoveUnit(udg_F280Caster)
 if udg_F280Target!=null then
  call RemoveUnit(udg_F280Target)
 endif
 set udg_F280Caster=null
 set udg_F280Target=null
 call F280Mark("end-cleanup")
endfunction
function F280Tick takes nothing returns nothing
 set udg_F280Tick=udg_F280Tick+1
 if udg_F280Tick==1 then
  call F280Mark("start")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_F280Tick==20 then
  set udg_F280Scene=0
  call F280Setup()
 elseif udg_F280Scene>=0 then
  set udg_F280Local=udg_F280Local+1
  if udg_F280Local==10 then
   if udg_F280Scene==1 or udg_F280Scene==4 then
    call IssueImmediateOrder(udg_F280Caster,"stop")
    call F280Sample("stopped")
   elseif udg_F280Scene==2 then
    call RemoveUnit(udg_F280Target)
    set udg_F280Target=null
    call F280Sample("removed")
   endif
  endif
  call F280Sample("sample")
  if udg_F280Local==100 then
   call F280Cleanup()
   set udg_F280Local=0
   set udg_F280Scene=udg_F280Scene+1
   if udg_F280Scene==6 then
    call F280Mark("complete")
    call PauseTimer(udg_F280Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call F280Setup()
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
 set udg_F280Timer=CreateTimer()
 call TimerStart(udg_F280Timer,0.1,true,function F280Tick)
endfunction
