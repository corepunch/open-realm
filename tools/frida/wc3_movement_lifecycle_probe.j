globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeSpeed takes real speed returns nothing
 call SetUnitMoveSpeed(udg_PathProbeUnit,speed)
 call Preload("PATHSPEEDVALUE tick="+I2S(udg_PathProbeTick)+" value="+R2S(GetUnitMoveSpeed(udg_PathProbeUnit)))
 call PathProbeRecord("speed_change")
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1616.0,1712.0)
  call PathProbeRecord("initial_move")
 elseif udg_PathProbeTick==25 then
  call PathProbeSpeed(250.0)
 elseif udg_PathProbeTick==45 then
  call PathProbeSpeed(400.0)
 elseif udg_PathProbeTick==55 then
  call IssuePointOrder(udg_PathProbeUnit,"move",480.0,1760.0)
  call PathProbeRecord("moving_retarget")
 elseif udg_PathProbeTick==70 or udg_PathProbeTick==175 then
  call PathProbeSpeed(0.0)
 elseif udg_PathProbeTick==75 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1440.0,384.0)
  call PathProbeRecord("stationary_retarget")
 elseif udg_PathProbeTick==95 or udg_PathProbeTick==205 then
  call PathProbeSpeed(150.0)
 elseif udg_PathProbeTick==110 or udg_PathProbeTick==210 then
  call PathProbeRecord("before_stop")
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call PathProbeRecord("after_stop")
  call Preload("PATHPOSE case=boundary_reset")
  call SetUnitX(udg_PathProbeUnit,640.0)
  call SetUnitY(udg_PathProbeUnit,608.0)
  call Preload("PATHPOSE done=boundary_reset")
  call PathProbeRecord("boundary_reset")
 elseif udg_PathProbeTick==120 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1760.0,448.0)
  call PathProbeRecord("boundary_restart")
 elseif udg_PathProbeTick==135 then
  call PathProbeSpeed(200.0)
 elseif udg_PathProbeTick==150 then
  call IssuePointOrder(udg_PathProbeUnit,"move",320.0,1728.0)
  call PathProbeRecord("moving_retarget")
 elseif udg_PathProbeTick==180 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1504.0,320.0)
  call PathProbeRecord("stationary_retarget")
 elseif udg_PathProbeTick==195 then
  call PathProbeSpeed(300.0)
 elseif udg_PathProbeTick==220 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1040.0,1040.0)
  call PathProbeRecord("boundary_restart")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call Preload("PATHSPEED done=long_lifecycle")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_movement_lifecycle")
 call Preload("PATHSPEED case=long_lifecycle")
 call PathProbeSpeed(150.0)
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
