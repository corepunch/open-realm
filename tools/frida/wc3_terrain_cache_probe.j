globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathTerrainPatch takes integer first returns nothing
 local integer x=first
 local integer y=first
 loop
  exitwhen y==first+4
  set x=first
  loop
   exitwhen x==first+4
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,false)
   set x=x+1
  endloop
  set y=y+1
 endloop
endfunction
function PathProbeTick takes nothing returns nothing
 local destructable tree=null
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1008.0,1040.0)
  call PathProbeRecord("initial_move")
 elseif udg_PathProbeTick==20 then
  call PathTerrainPatch(18)
  call PathTerrainPatch(40)
  call Preload("PATHLIFE label=terrain_edits")
  call PathProbeRecord("terrain_edits")
 elseif udg_PathProbeTick==85 or udg_PathProbeTick==185 then
  call IssueImmediateOrder(udg_PathProbeUnit,"stop")
  call SetUnitX(udg_PathProbeUnit,272.0)
  call SetUnitY(udg_PathProbeUnit,304.0)
  call PathProbeRecord("reset")
 elseif udg_PathProbeTick==100 then
  set tree=CreateDestructable('LTlt',640.0,640.0,0.0,1.0,0)
  call Preload("PATHLIFE label=first_footprint_insert")
  call RemoveDestructable(tree)
  call Preload("PATHLIFE label=first_footprint_remove")
  call IssuePointOrder(udg_PathProbeUnit,"move",1008.0,1040.0)
  call PathProbeRecord("first_refresh_move")
 elseif udg_PathProbeTick==200 then
  set tree=CreateDestructable('LTlt',1344.0,1344.0,0.0,1.0,0)
  call Preload("PATHLIFE label=second_footprint_insert")
  call RemoveDestructable(tree)
  call Preload("PATHLIFE label=second_footprint_remove")
  call IssuePointOrder(udg_PathProbeUnit,"move",1008.0,1040.0)
  call PathProbeRecord("second_refresh_move")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call Preload("PATHLIFE label=complete")
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,100.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_terrain_cache")
 call Preload("PATHLIFE label=baseline")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
