globals
 hashtable udg_JourneyTable=null
 integer udg_PathProbeTick=0
 timer udg_PathProbeTimer=null
endglobals
// Pathing-map probe: make_wc3_journey_map.py (spec "probe": "pathing", "box", "step") fills
// the grid. One row per timer tick exports IsTerrainPathable at each cell centre as bits
// 1 walk, 2 fly, 4 build, 8 amphibious (set = not pathable, the native's own sense).
function PathingCell takes real x, real y returns integer
 local integer bits=0
 if IsTerrainPathable(x,y,PATHING_TYPE_WALKABILITY) then
  set bits=bits+1
 endif
 if IsTerrainPathable(x,y,PATHING_TYPE_FLYABILITY) then
  set bits=bits+2
 endif
 if IsTerrainPathable(x,y,PATHING_TYPE_BUILDABILITY) then
  set bits=bits+4
 endif
 if IsTerrainPathable(x,y,PATHING_TYPE_AMPHIBIOUSPATHING) then
  set bits=bits+8
 endif
 return bits
endfunction
function PathingRow takes integer row returns nothing
 local integer col=0
 loop
  exitwhen col>=@COLS@
  call SaveInteger(udg_JourneyTable,row,col,PathingCell(@X0@+I2R(col)*@STEP@,@Y0@+I2R(row)*@STEP@))
  set col=col+1
 endloop
 call Preload("PATHMETA ints first="+I2S(row)+" count=1 cols=@COLS@")
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick>=2 and udg_PathProbeTick<2+@ROWS@ then
  call PathingRow(udg_PathProbeTick-2)
 endif
 if udg_PathProbeTick==2+@ROWS@ then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_pathing")
 set udg_JourneyTable=InitHashtable()
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call Preload("PATHTRACE tick=0 label=start_journey")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
