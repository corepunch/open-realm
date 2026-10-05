globals
 unit udg_PathProbeUnit=null
 unit array udg_PathProbeCrowd
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 local real goal=1728.0
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 or udg_PathProbeTick==100 or udg_PathProbeTick==190 then
  if udg_PathProbeTick==190 then
   set goal=272.0
  endif
  call PathProbeRecord("wave_before")
  loop
   exitwhen i==96
   call Preload("PATHSCHED tick="+I2S(udg_PathProbeTick)+" member="+I2S(i)+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_PathProbeCrowd[i])))+" handle="+I2S(GetHandleId(udg_PathProbeCrowd[i])))
   call IssuePointOrder(udg_PathProbeCrowd[i],"move",goal,128.0+I2R(i/8)*64.0)
   set i=i+1
  endloop
  call PathProbeRecord("wave_after")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,true)
 loop
  exitwhen i==96
  set udg_PathProbeCrowd[i]=CreateUnit(Player(ModuloInteger(i,2)),'hfoo',128.0+I2R(ModuloInteger(i,8))*64.0,128.0+I2R(i/8)*64.0,0.0)
  call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  set i=i+1
 endloop
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_scheduler_contention")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
