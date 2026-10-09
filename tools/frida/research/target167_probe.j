globals
 timer udg_T167Timer=null
 integer udg_T167Tick=0
 integer udg_T167Scene=-1
 integer udg_T167Local=0
 unit array udg_T167F
 unit udg_T167T=null
endglobals
function T167Mark takes string label returns nothing
 call Preload("T167 tick="+I2S(udg_T167Tick)+" s="+I2S(udg_T167Scene)+" l="+I2S(udg_T167Local)+" label="+label)
endfunction
function T167Sample takes string label returns nothing
 call Preload("T167 tick="+I2S(udg_T167Tick)+" s="+I2S(udg_T167Scene)+" l="+I2S(udg_T167Local)+" label="+label+" orders="+I2S(GetUnitCurrentOrder(udg_T167F[0]))+","+I2S(GetUnitCurrentOrder(udg_T167F[1]))+","+I2S(GetUnitCurrentOrder(udg_T167F[2])))
endfunction
function T167Cleanup takes nothing returns nothing
 local integer i=0
 call T167Mark("begin-cleanup")
 loop
  exitwhen i==3
  call RemoveUnit(udg_T167F[i])
  set udg_T167F[i]=null
  set i=i+1
 endloop
 call RemoveUnit(udg_T167T)
 set udg_T167T=null
 call T167Mark("end-cleanup")
endfunction
function T167Setup takes nothing returns nothing
 local integer i=0
 call T167Mark("begin-setup")
 set udg_T167T=CreateUnit(Player(0),'hfoo',1568,288,0)
 loop
  exitwhen i==3
  set udg_T167F[i]=CreateUnit(Player(0),'hfoo',1344,288+I2R(i)*160,0)
  set i=i+1
 endloop
 call T167Mark("created")
 if udg_T167Scene==0 then
  call IssueTargetOrder(udg_T167F[2],"move",udg_T167T)
  call IssueTargetOrder(udg_T167F[0],"move",udg_T167T)
  call IssueTargetOrder(udg_T167F[1],"move",udg_T167T)
 else
  call IssueTargetOrder(udg_T167F[0],"move",udg_T167T)
  call IssueTargetOrder(udg_T167F[2],"move",udg_T167T)
  call IssueTargetOrder(udg_T167F[1],"move",udg_T167T)
 endif
 call T167Sample("issued")
endfunction
function T167Tick takes nothing returns nothing
 set udg_T167Tick=udg_T167Tick+1
 if udg_T167Tick==20 then
  set udg_T167Scene=0
  set udg_T167Local=0
  call T167Setup()
 elseif udg_T167Scene>=0 then
  set udg_T167Local=udg_T167Local+1
  if udg_T167Local==30 and udg_T167Scene==2 then
   call IssueImmediateOrder(udg_T167F[0],"stop")
   call IssueTargetOrder(udg_T167F[0],"move",udg_T167T)
   call T167Sample("reissued")
  endif
  if udg_T167Local==60 then
   call T167Sample("before-hide")
   call ShowUnit(udg_T167T,false)
   call T167Sample("after-hide")
  endif
  if udg_T167Local==80 then
   call ShowUnit(udg_T167T,true)
   call T167Sample("after-show")
  endif
  if udg_T167Local==100 then
   call T167Cleanup()
   set udg_T167Scene=udg_T167Scene+1
   set udg_T167Local=0
   if udg_T167Scene==3 then
    call T167Mark("complete")
    call PauseTimer(udg_T167Timer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call T167Setup()
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
 set udg_T167Timer=CreateTimer()
 call TimerStart(udg_T167Timer,0.1,true,function T167Tick)
endfunction
