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
function PathProbeBoundary takes integer count, real x, real y returns nothing
 local integer i=0
 call GroupClear(udg_PathProbeGroup)
 loop
  exitwhen i==count
  call GroupAddUnit(udg_PathProbeGroup,udg_PathProbeCrowd[i])
  set i=i+1
 endloop
 call Preload("PATHBOUNDARY count="+I2S(count)+" label=before")
 call GroupPointOrder(udg_PathProbeGroup,"move",x,y)
 call Preload("PATHBOUNDARY count="+I2S(count)+" label=after")
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call PathProbeBoundary(11,1728.0,1536.0)
 elseif udg_PathProbeTick==20 then
  call PathProbeBoundary(12,256.0,1536.0)
 elseif udg_PathProbeTick==30 then
  call PathProbeBoundary(13,1536.0,1536.0)
 elseif udg_PathProbeTick==40 then
  call PathProbeBoundary(25,1536.0,256.0)
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==45 then
  loop
   exitwhen i==25
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
 call Preload("PATHTRACE tick=0 label=start_formation_boundary x=0 y=0 order=0")
 set udg_PathProbeGroup=CreateGroup()
 loop
  exitwhen i==25
  set udg_PathProbeCrowd[i]=CreateUnit(Player(0),'hF00'+ModuloInteger(i,4),128.0+I2R(i/5)*96.0,128.0+I2R(ModuloInteger(i,5))*96.0,0.0)
  call SetUnitMoveSpeed(udg_PathProbeCrowd[i],150.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  call IssuePointOrder(udg_PathProbeCrowd[i],"move",1792.0,128.0+I2R(ModuloInteger(i,5))*96.0)
  set i=i+1
 endloop
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call PathProbeRecord("created")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
