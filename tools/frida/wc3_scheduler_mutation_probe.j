globals
 unit udg_PathProbeUnit=null
 unit array udg_PathProbeCrowd
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeQueue takes string label returns nothing
 call Preload("PATHQUEUE tick="+I2S(udg_PathProbeTick)+" label="+label)
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call PathProbeRecord("wave_before")
  loop
   exitwhen i==96
   call IssuePointOrder(udg_PathProbeCrowd[i],"move",1728.0,128.0+I2R(i/8)*64.0)
   set i=i+1
  endloop
  call PathProbeRecord("wave_after")
 elseif udg_PathProbeTick==11 then
  call PathProbeQueue("before_stop")
  call IssueImmediateOrder(udg_PathProbeCrowd[52],"stop")
  call PathProbeQueue("after_stop")
  call IssuePointOrder(udg_PathProbeCrowd[52],"move",1728.0,512.0)
  call PathProbeQueue("after_reissue")
  call SetUnitOwner(udg_PathProbeCrowd[50],Player(1),false)
  call PathProbeQueue("after_owner")
  call IssuePointOrder(udg_PathProbeCrowd[50],"move",1728.0,512.0)
  call PathProbeQueue("after_owner_reissue")
  call RemoveUnit(udg_PathProbeCrowd[48])
  set udg_PathProbeCrowd[48]=null
  call PathProbeQueue("after_remove")
 elseif udg_PathProbeTick==12 then
  call PathProbeQueue("next_tick")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==40 then
  set i=0
  loop
   exitwhen i==96
   if udg_PathProbeCrowd[i]!=null then
    call IssueImmediateOrder(udg_PathProbeCrowd[i],"stop")
    call PauseUnit(udg_PathProbeCrowd[i],true)
   endif
   set i=i+1
  endloop
  call PathProbeQueue("final_stopped")
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
 call PathProbeRecord("start_scheduler_mutation")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
