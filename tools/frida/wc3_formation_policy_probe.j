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
 set udg_PathProbeTick=udg_PathProbeTick+1
 call PathProbeRecord("sample")
 if udg_PathProbeTick==120 then
  loop
   exitwhen i==6
   call IssueImmediateOrder(udg_PathProbeCrowd[i],"stop")
   call PauseUnit(udg_PathProbeCrowd[i],true)
   set i=i+1
  endloop
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call EnableUserControl(true)
 call ShowInterface(true,0.0)
 call ClearSelection()
 call Preload("PATHTRACE tick=0 label=start_formation_policy x=0 y=0 order=0")
 loop
  exitwhen i==6
  set udg_PathProbeCrowd[i]=CreateUnit(GetLocalPlayer(),'hF00'+ModuloInteger(i,4),128.0,128.0+I2R(i)*64.0,0.0)
  if i<3 then
   call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)
  else
   call SetUnitMoveSpeed(udg_PathProbeCrowd[i],350.0)
  endif
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  call SelectUnit(udg_PathProbeCrowd[i],true)
  set i=i+1
 endloop
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call SetCameraPosition(512.0,512.0)
 call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
 call PathProbeRecord("created")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
