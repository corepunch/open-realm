globals
 unit udg_PathProbeUnit=null
 unit array udg_PathProbeCrowd
 unit array udg_PathProbeTargets
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==5
  call SetWidgetLife(udg_PathProbeTargets[i],420.0)
  set i=i+1
 endloop
 if udg_PathProbeTick==10 then
  call PathProbeRecord("attacks_before")
  call IssueTargetOrder(udg_PathProbeCrowd[0],"attack",udg_PathProbeTargets[0])
  call IssuePointOrder(udg_PathProbeCrowd[1],"attackground",1408.0,384.0)
  call IssueTargetOrder(udg_PathProbeCrowd[2],"attack",udg_PathProbeTargets[2])
  call IssueTargetOrder(udg_PathProbeCrowd[3],"attack",udg_PathProbeTargets[3])
  call IssuePointOrder(udg_PathProbeCrowd[4],"shockwave",800.0,1664.0)
  call PathProbeRecord("attacks_after")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==100 then
  set i=0
  loop
   exitwhen i==5
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
 set udg_PathProbeCrowd[0]=CreateUnit(Player(0),'earc',128.0,128.0,0.0)
 set udg_PathProbeCrowd[1]=CreateUnit(Player(0),'hmtm',1152.0,128.0,90.0)
 set udg_PathProbeCrowd[2]=CreateUnit(Player(0),'ucry',128.0,896.0,0.0)
 set udg_PathProbeCrowd[3]=CreateUnit(Player(0),'ebal',1152.0,896.0,90.0)
 set udg_PathProbeCrowd[4]=CreateUnit(Player(0),'Otch',128.0,1664.0,0.0)
 call SelectHeroSkill(udg_PathProbeCrowd[4],'AOsh')
 loop
  exitwhen i==5
  set udg_PathProbeTargets[i]=CreateUnit(Player(1),'hfoo',384.0+I2R(ModuloInteger(i,2))*1024.0,128.0+I2R(i/2)*768.0,180.0)
  call SetUnitAcquireRange(udg_PathProbeCrowd[i],0.0)
  call IssueImmediateOrder(udg_PathProbeTargets[i],"holdposition")
  call PauseUnit(udg_PathProbeTargets[i],true)
  set i=i+1
 endloop
 call SetUnitPosition(udg_PathProbeTargets[3],1408.0,1152.0)
 set udg_PathProbeUnit=udg_PathProbeCrowd[0]
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_scheduler_nonunit")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
