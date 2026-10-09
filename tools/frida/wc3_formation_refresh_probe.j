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
  call PathProbeRecord("first_order_before")
  call GroupPointOrder(udg_PathProbeGroup,"move",1728.0,512.0)
  call PathProbeRecord("first_order_after")
 elseif udg_PathProbeTick==20 then
  call PathProbeRecord("resize_before")
  call UnitAddAbility(udg_PathProbeCrowd[0],'AF03')
  call PathProbeRecord("resize_after")
 elseif udg_PathProbeTick==30 then
  call PathProbeRecord("remove_before")
  call RemoveUnit(udg_PathProbeCrowd[1])
  set udg_PathProbeCrowd[1]=null
  call PathProbeRecord("remove_after")
 elseif udg_PathProbeTick==50 then
  call PathProbeRecord("retarget_before")
  call GroupPointOrder(udg_PathProbeGroup,"move",1536.0,1792.0)
  call PathProbeRecord("retarget_after")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==180 then
  loop
   exitwhen i==6
   if udg_PathProbeCrowd[i]!=null then
    call IssueImmediateOrder(udg_PathProbeCrowd[i],"stop")
    call PauseUnit(udg_PathProbeCrowd[i],true)
   endif
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
 call Preload("PATHTRACE tick=0 label=start_formation_refresh x=0 y=0 order=0")
 set udg_PathProbeGroup=CreateGroup()
 loop
  exitwhen i==6
  set udg_PathProbeCrowd[i]=CreateUnit(Player(0),'hF00'+ModuloInteger(i,3),128.0,128.0+I2R(i)*64.0,0.0)
  call SetUnitMoveSpeed(udg_PathProbeCrowd[i],150.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeCrowd[i])
  set i=i+1
 endloop
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call PathProbeRecord("created")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
