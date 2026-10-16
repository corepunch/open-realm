globals
 unit udg_PathProbeUnit=null
 unit array udg_PathProbeCrowd
 group udg_PathProbeGroup=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call PathProbeRecord("first_group_before")
  call GroupPointOrder(udg_PathProbeGroup,"move",1728.0,512.0)
  call PathProbeRecord("first_group_after")
 elseif udg_PathProbeTick==15 then
  call PathProbeRecord("rebind_before")
  call UnitAddAbility(udg_PathProbeCrowd[0],'AF03')
  call PathProbeRecord("rebind_return")
 elseif udg_PathProbeTick==16 then
  call Preload("PATHRANK label=rebound type="+I2S(GetUnitTypeId(udg_PathProbeCrowd[0])))
 elseif udg_PathProbeTick==20 then
  call PathProbeRecord("second_group_before")
  call GroupPointOrder(udg_PathProbeGroup,"move",512.0,1728.0)
  call PathProbeRecord("second_group_after")
 elseif udg_PathProbeTick==30 then
  call Preload("PATHRANK label=fresh_before")
  set udg_PathProbeCrowd[6]=CreateUnit(Player(0),'hF03',1024.0,128.0,0.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[6],0.0)
  call Preload("PATHRANK label=fresh_after type="+I2S(GetUnitTypeId(udg_PathProbeCrowd[6])))
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==40 then
  loop
   exitwhen i==7
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
 local integer kind=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("PATHTRACE tick=0 label=start_formation_ranks x=0 y=0 order=0")
 set udg_PathProbeGroup=CreateGroup()
 loop
  exitwhen i==6
  set kind='hF00'+ModuloInteger(i,4)
  set udg_PathProbeCrowd[i]=CreateUnit(Player(0),kind,128.0,128.0+I2R(i)*64.0,0.0)
  call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeCrowd[i])
  set i=i+1
 endloop
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("created")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
