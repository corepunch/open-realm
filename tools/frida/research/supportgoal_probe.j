globals
 timer udg_SGTimer=null
 integer udg_SGTick=0
 integer udg_SGScene=-1
 integer udg_SGLocal=0
 integer udg_SGIdle=0
 unit udg_SGUnit=null
 unit udg_SGTarget=null
 destructable udg_SGBridge=null
endglobals
function SGMark takes string label returns nothing
 call Preload("SG tick="+I2S(udg_SGTick)+" scene="+I2S(udg_SGScene)+" l="+I2S(udg_SGLocal)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_SGUnit))+" x="+R2SW(GetUnitX(udg_SGUnit),1,4)+" y="+R2SW(GetUnitY(udg_SGUnit),1,4))
endfunction
function SGSetup takes nothing returns nothing
 local integer kind='hQ0A'+udg_SGScene/10
 local integer scenario=ModuloInteger(udg_SGScene,10)
 local real x=1024
 local real y=192
 local boolean accepted=false
 call SGMark("begin-setup")
 set udg_SGUnit=CreateUnit(Player(0),kind,576,640,0)
 call SetUnitAcquireRange(udg_SGUnit,0)
 if scenario==1 then
  set y=640
 elseif scenario==2 then
  set x=1856
  set y=640
 elseif scenario==3 then
  set x=256
 elseif scenario==4 then
  set x=1408
  set y=1856
 elseif scenario==5 then
  set y=-512
 elseif scenario==6 or scenario==7 then
  set udg_SGTarget=CreateUnit(Player(0),'hQ0R',1024,192,0)
  call SetUnitAcquireRange(udg_SGTarget,0)
  call PauseUnit(udg_SGTarget,true)
 elseif scenario==9 then
  call SetUnitPathing(udg_SGUnit,false)
 endif
 call SGMark("created")
 if scenario==6 or scenario==7 then
  set accepted=IssueTargetOrder(udg_SGUnit,"move",udg_SGTarget)
 elseif scenario==8 then
  set accepted=IssueTargetOrder(udg_SGUnit,"move",udg_SGBridge)
 else
  set accepted=IssuePointOrder(udg_SGUnit,"move",x,y)
 endif
 if accepted then
  call SGMark("accepted")
 else
  call SGMark("rejected")
 endif
endfunction
function SGDone takes nothing returns boolean
 local integer scenario=ModuloInteger(udg_SGScene,10)
 if scenario==6 or scenario==8 then
  return udg_SGLocal==80 or udg_SGIdle==5
 endif
 return udg_SGLocal==150 or udg_SGIdle==5
endfunction
function SGTick takes nothing returns nothing
 set udg_SGTick=udg_SGTick+1
 if udg_SGTick==1 then
  call SGMark("start")
  call PreloadGenEnd("supportgoal-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_SGTick==20 then
  set udg_SGScene=0
  call SGSetup()
 elseif udg_SGScene>=0 then
  set udg_SGLocal=udg_SGLocal+1
  if udg_SGLocal==10 and ModuloInteger(udg_SGScene,10)==7 then
   call SGMark("before-removal")
   call RemoveUnit(udg_SGTarget)
   set udg_SGTarget=null
   call SGMark("after-removal")
  endif
  call SGMark("sample")
  if GetUnitCurrentOrder(udg_SGUnit)==0 then
   set udg_SGIdle=udg_SGIdle+1
  else
   set udg_SGIdle=0
  endif
  if SGDone() then
   call SGMark("begin-cleanup")
   call RemoveUnit(udg_SGUnit)
   call RemoveUnit(udg_SGTarget)
   set udg_SGUnit=null
   set udg_SGTarget=null
   call SGMark("end-cleanup")
   set udg_SGLocal=0
   set udg_SGIdle=0
   set udg_SGScene=udg_SGScene+1
   if udg_SGScene==200 then
    call SGMark("complete")
    call PauseTimer(udg_SGTimer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call SGSetup()
   endif
  endif
 endif
endfunction
function SGFindBridge takes nothing returns nothing
 if GetDestructableTypeId(GetEnumDestructable())=='LT06' then
  set udg_SGBridge=GetEnumDestructable()
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call EnumDestructablesInRect(Rect(0,0,2048,2048),null,function SGFindBridge)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_SGTimer=CreateTimer()
 call TimerStart(udg_SGTimer,0.1,true,function SGTick)
endfunction
