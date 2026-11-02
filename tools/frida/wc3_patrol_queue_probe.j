globals
 unit array udg_PathPatrolSubjects
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathPatrolRow=0
 hashtable udg_PathPatrolTable=null
endglobals
function PathPatrolRecord takes integer i, string label returns nothing
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,1,i)
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,2,GetUnitCurrentOrder(udg_PathPatrolSubjects[i]))
 call SaveReal(udg_PathPatrolTable,udg_PathPatrolRow,3,GetUnitX(udg_PathPatrolSubjects[i]))
 call SaveReal(udg_PathPatrolTable,udg_PathPatrolRow,4,GetUnitY(udg_PathPatrolSubjects[i]))
 call SaveInteger(udg_PathPatrolTable,udg_PathPatrolRow,5,GetHandleId(udg_PathPatrolSubjects[i]))
 call Preload("PATHMETA patrol_queue row="+I2S(udg_PathPatrolRow)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label)
 set udg_PathPatrolRow=udg_PathPatrolRow+1
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==2
  if udg_PathProbeTick==1+i*150 then
   call ClearSelection()
   call SelectUnit(udg_PathPatrolSubjects[i],true)
   call SetCameraPosition(1008.0,320.0+I2R(i)*960.0)
   if i==0 then
    call IssuePointOrder(udg_PathPatrolSubjects[i],"move",1008.0,320.0)
   else
    call IssuePointOrder(udg_PathPatrolSubjects[i],"patrol",1008.0,1280.0)
   endif
   call PathPatrolRecord(i,"order")
  endif
  call PathPatrolRecord(i,"sample")
  set i=i+1
 endloop
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=patrol_queue")
 if udg_PathProbeTick==300 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call Preload("PATHMETA case=metadata_patrol_queue")
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call SetCameraBounds(-1024.0,-1024.0,3072.0,3072.0,-1024.0,3072.0,3072.0,-1024.0)
 set udg_PathPatrolTable=InitHashtable()
 loop
  exitwhen i==2
  set udg_PathPatrolSubjects[i]=CreateUnit(GetLocalPlayer(),'hfoo',272.0,320.0+I2R(i)*960.0,0.0)
  call SetUnitMoveSpeed(udg_PathPatrolSubjects[i],100.0)
  call SetUnitAcquireRange(udg_PathPatrolSubjects[i],0.0)
  call PathPatrolRecord(i,"initial")
  set i=i+1
 endloop
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("PATHTRACE tick=0 label=start_patrol_queue")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
