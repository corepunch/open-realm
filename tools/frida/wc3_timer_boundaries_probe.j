globals
 timer array udg_PathInputTimer
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 unit udg_PathProbeUnit=null
endglobals
function PathTimerRead takes integer i returns nothing
 call Preload("PATHTIMER read="+I2S(i)+" tick="+I2S(udg_PathProbeTick))
 call Preload("PATHTIMER timeout="+R2S(TimerGetTimeout(udg_PathInputTimer[i]))+" elapsed="+R2S(TimerGetElapsed(udg_PathInputTimer[i]))+" remaining="+R2S(TimerGetRemaining(udg_PathInputTimer[i])))
endfunction
function PathTimerAdd takes integer i, real timeout returns nothing
 set udg_PathInputTimer[i]=CreateTimer()
 call Preload("PATHTIMER start="+I2S(i))
 call TimerStart(udg_PathInputTimer[i],timeout,false,null)
 call PathTimerRead(i)
endfunction
function PathTimerMove takes nothing returns nothing
 call Preload("PATHTIMER move tick="+I2S(udg_PathProbeTick))
 call SetUnitMoveSpeed(udg_PathProbeUnit,TimerGetTimeout(GetExpiredTimer())*2000.0)
 call IssuePointOrder(udg_PathProbeUnit,"move",640.0,512.0)
endfunction
function PathTimerTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==13
  if (udg_PathProbeTick==11 or udg_PathProbeTick==290) then
   call Preload("PATHTIMER pause="+I2S(i))
   call PauseTimer(udg_PathInputTimer[i])
  endif
  if (udg_PathProbeTick==22 or udg_PathProbeTick==305) then
   call Preload("PATHTIMER resume="+I2S(i))
   call ResumeTimer(udg_PathInputTimer[i])
  endif
  call PathTimerRead(i)
  set i=i+1
 endloop
 if udg_PathProbeTick==299 then
  call SetUnitMoveSpeed(udg_PathProbeUnit,TimerGetElapsed(udg_PathInputTimer[10])*0.8)
  call IssuePointOrder(udg_PathProbeUnit,"move",1280.0,800.0)
 endif
 if udg_PathProbeTick==305 then
  call SetUnitMoveSpeed(udg_PathProbeUnit,TimerGetRemaining(udg_PathInputTimer[10])*10.0)
  call IssuePointOrder(udg_PathProbeUnit,"move",512.0,1280.0)
 endif
 if udg_PathProbeTick==350 then
  call Preload("PATHTIMER complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local timer moveTimer=CreateTimer()
 call Preload("PATHTRACE tick=0 label=start_timer_inputs scenario=@SCENARIO@")
 set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)
 call PathTimerAdd(0,-0.1)
 call PathTimerAdd(1,-0.001)
 call PathTimerAdd(2,0.0)
 call PathTimerAdd(3,0.0001)
 call PathTimerAdd(4,0.004999999)
 call PathTimerAdd(5,0.005)
 call PathTimerAdd(6,0.005000001)
 call PathTimerAdd(7,0.099999994)
 call PathTimerAdd(8,0.1)
 call PathTimerAdd(9,0.100000001)
 call PathTimerAdd(10,299.99997)
 call PathTimerAdd(11,300.00003)
 call PathTimerAdd(12,1000000.25)
 call TimerStart(moveTimer,0.1,false,function PathTimerMove)
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,1.0,true,function PathTimerTick)
endfunction
