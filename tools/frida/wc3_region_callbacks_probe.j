globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathProbeMode=0
 region udg_PathProbeRegion=null
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeEnter takes nothing returns nothing
 if GetTriggerUnit()!=udg_PathProbeUnit then
  return
 endif
 call PathProbeRecord("region_enter_before")
 if udg_PathProbeMode==1 then
  call SetUnitPosition(udg_PathProbeUnit,1344.0,304.0)
  call PathProbeRecord("callback_teleport")
  call IssuePointOrder(udg_PathProbeUnit,"move",1568.0,1040.0)
  call PathProbeRecord("callback_new_move")
 elseif udg_PathProbeMode==2 then
  call RemoveUnit(udg_PathProbeUnit)
  set udg_PathProbeUnit=null
  call PathProbeRecord("callback_removed")
 endif
 call PathProbeRecord("region_enter_after")
endfunction
function PathProbeLeave takes nothing returns nothing
 if GetTriggerUnit()==udg_PathProbeUnit then
  call PathProbeRecord("region_leave")
 endif
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 or udg_PathProbeTick==100 or udg_PathProbeTick==200 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1008.0,1040.0)
  call PathProbeRecord("point_move")
 elseif udg_PathProbeTick==85 or udg_PathProbeTick==185 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitX(udg_PathProbeUnit,272.0)
  call SetUnitY(udg_PathProbeUnit,304.0)
  set udg_PathProbeMode=udg_PathProbeMode+1
  call PathProbeRecord("reset")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call Preload("PATHPOSE done=region_callbacks")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger enter=CreateTrigger()
 local trigger leave=CreateTrigger()
 call Preload("PATHPOSE case=region_callbacks")
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 set udg_PathProbeRegion=CreateRegion()
 call RegionAddRect(udg_PathProbeRegion,Rect(640.0,672.0,704.0,736.0))
 call TriggerRegisterEnterRegion(enter,udg_PathProbeRegion,null)
 call TriggerRegisterLeaveRegion(leave,udg_PathProbeRegion,null)
 call TriggerAddAction(enter,function PathProbeEnter)
 call TriggerAddAction(leave,function PathProbeLeave)
 call PathProbeRecord("start_region_callbacks")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
