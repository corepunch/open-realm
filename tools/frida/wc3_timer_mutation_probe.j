globals
 timer array udg_PathInputTimer
 timer udg_PathProbeTimer=null
 unit udg_PathProbeUnit=null
 integer array udg_PathCalls
 integer udg_PathProbeTick=0
 integer udg_PathTimerRow=0
endglobals
function PathTimerIdentity takes timer t returns integer
 local integer i=0
 loop
  exitwhen i==12
  if udg_PathInputTimer[i]==t then
   return i
  endif
  set i=i+1
 endloop
 return 12
endfunction
function PathTimerRecord takes integer id returns nothing
 local timer t=GetExpiredTimer()
 call Preload("PATHTIMER record="+I2S(id)+" row="+I2S(udg_PathTimerRow)+" calls="+I2S(udg_PathCalls[PathTimerIdentity(t)]))
 call Preload("PATHTIMER timeout="+R2S(TimerGetTimeout(t))+" elapsed="+R2S(TimerGetElapsed(t))+" remaining="+R2S(TimerGetRemaining(t)))
 set udg_PathTimerRow=udg_PathTimerRow+1
endfunction
function PathTimerCondition takes nothing returns boolean
 call PathTimerRecord(200+PathTimerIdentity(GetExpiredTimer()))
 return true
endfunction
function PathTimerEvent takes nothing returns nothing
 call PathTimerRecord(100+PathTimerIdentity(GetExpiredTimer()))
endfunction
function PathTimerA takes nothing returns nothing
 set udg_PathCalls[0]=udg_PathCalls[0]+1
 call PathTimerRecord(0)
 call PauseTimer(udg_PathInputTimer[1])
 call TimerStart(udg_PathInputTimer[2],0.1,false,function PathTimerEvent)
 call SetUnitMoveSpeed(udg_PathProbeUnit,200.0)
 call IssuePointOrder(udg_PathProbeUnit,"move",640.0,512.0)
endfunction
function PathTimerB takes nothing returns nothing
 set udg_PathCalls[1]=udg_PathCalls[1]+1
 call PathTimerRecord(1)
 call IssuePointOrder(udg_PathProbeUnit,"move",800.0,640.0)
endfunction
function PathTimerC takes nothing returns nothing
 set udg_PathCalls[2]=udg_PathCalls[2]+1
 call PathTimerRecord(2)
endfunction
function PathTimerD takes nothing returns nothing
 set udg_PathCalls[3]=udg_PathCalls[3]+1
 call PathTimerRecord(3)
 if udg_PathCalls[3]==3 then
  call DestroyTimer(GetExpiredTimer())
  set udg_PathInputTimer[3]=null
 endif
endfunction
function PathTimerE takes nothing returns nothing
 set udg_PathCalls[4]=udg_PathCalls[4]+1
 call PathTimerRecord(4)
 if udg_PathCalls[4]>=25 then
  call PauseTimer(GetExpiredTimer())
 endif
endfunction
function PathTimerF takes nothing returns nothing
 set udg_PathCalls[5]=udg_PathCalls[5]+1
 call PathTimerRecord(5)
 if udg_PathCalls[5]==5 then
  call DestroyTimer(GetExpiredTimer())
  set udg_PathInputTimer[5]=null
 endif
endfunction
function PathTimerGrace takes nothing returns nothing
 set udg_PathCalls[6]=udg_PathCalls[6]+1
 call PathTimerRecord(16)
endfunction
function PathTimerG takes nothing returns nothing
 set udg_PathCalls[6]=udg_PathCalls[6]+1
 call PathTimerRecord(6)
 if udg_PathCalls[6]==4 then
  call TimerStart(GetExpiredTimer(),0.07,false,function PathTimerGrace)
 endif
endfunction
function PathTimerH takes nothing returns nothing
 set udg_PathCalls[7]=udg_PathCalls[7]+1
 call PathTimerRecord(7)
 call ResumeTimer(udg_PathInputTimer[4])
 call IssueImmediateOrder(udg_PathProbeUnit,"stop")
 call IssuePointOrder(udg_PathProbeUnit,"move",640.0,512.0)
endfunction
function PathTimerI takes nothing returns nothing
 set udg_PathCalls[8]=udg_PathCalls[8]+1
 call PathTimerRecord(8)
 call ResumeTimer(udg_PathInputTimer[1])
 call PauseTimer(udg_PathInputTimer[2])
 call ResumeTimer(udg_PathInputTimer[2])
endfunction
function PathTimerJ takes nothing returns nothing
 set udg_PathCalls[9]=udg_PathCalls[9]+1
 call PathTimerRecord(9)
 call DestroyTimer(udg_PathInputTimer[10])
 set udg_PathInputTimer[10]=null
 call TimerStart(udg_PathInputTimer[11],0.0,false,function PathTimerGrace)
endfunction
function PathTimerK takes nothing returns nothing
 set udg_PathCalls[10]=udg_PathCalls[10]+1
 call PathTimerRecord(10)
endfunction
function PathTimerEnd takes nothing returns nothing
 set udg_PathCalls[11]=udg_PathCalls[11]+1
 call PathTimerRecord(11)
endfunction
function PathTimerTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 call PathTimerRecord(12)
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1280.0,512.0)
 endif
 if udg_PathProbeTick==60 then
  call Preload("PATHTIMER complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 local trigger t=null
 call Preload("PATHTRACE tick=0 label=start_timer_mutation scenario=@SCENARIO@")
 call Preload("PATHTIMER begin")
 set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)
 loop
  exitwhen i==12
  set udg_PathInputTimer[i]=CreateTimer()
  set t=CreateTrigger()
  call TriggerRegisterTimerExpireEvent(t,udg_PathInputTimer[i])
  call TriggerAddCondition(t,Condition(function PathTimerCondition))
  call TriggerAddAction(t,function PathTimerEvent)
  set i=i+1
 endloop
 call TimerStart(udg_PathInputTimer[0],0.1,false,function PathTimerA)
 call TimerStart(udg_PathInputTimer[1],0.1,true,function PathTimerB)
 call TimerStart(udg_PathInputTimer[2],0.1,false,function PathTimerC)
 call TimerStart(udg_PathInputTimer[3],0.05,true,function PathTimerD)
 call TimerStart(udg_PathInputTimer[4],0.0001,true,function PathTimerE)
 call TimerStart(udg_PathInputTimer[5],0.0,true,function PathTimerF)
 call TimerStart(udg_PathInputTimer[6],-0.1,true,function PathTimerG)
 call TimerStart(udg_PathInputTimer[7],0.15,false,function PathTimerH)
 call TimerStart(udg_PathInputTimer[8],0.25,false,function PathTimerI)
 call TimerStart(udg_PathInputTimer[9],0.5,false,function PathTimerJ)
 call TimerStart(udg_PathInputTimer[10],0.5,true,function PathTimerK)
 call TimerStart(udg_PathInputTimer[11],1.0,false,function PathTimerEnd)
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathTimerTick)
endfunction
