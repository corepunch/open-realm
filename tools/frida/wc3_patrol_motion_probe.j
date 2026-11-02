globals
 unit array udg_PathMotionUnits
 boolean array udg_PathMotionPatrol
 real array udg_PathMotionAX
 real array udg_PathMotionAY
 real array udg_PathMotionBX
 real array udg_PathMotionBY
 boolean array udg_PathMotionTowardB
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathMotionRow=0
 hashtable udg_PathMotionTable=null
endglobals
// Paired Move/Patrol subjects on identical geometry. Move subjects are reissued
// toward the opposite endpoint once idle, so both kinds traverse alternating legs.
function PathMotionRecord takes integer i, string label returns nothing
 local unit u=udg_PathMotionUnits[i]
 call SaveInteger(udg_PathMotionTable,udg_PathMotionRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathMotionTable,udg_PathMotionRow,1,i)
 call SaveInteger(udg_PathMotionTable,udg_PathMotionRow,2,GetUnitCurrentOrder(u))
 call SaveReal(udg_PathMotionTable,udg_PathMotionRow,3,GetUnitFacing(u))
 call SaveReal(udg_PathMotionTable,udg_PathMotionRow,4,GetUnitX(u))
 call SaveReal(udg_PathMotionTable,udg_PathMotionRow,5,GetUnitY(u))
 call SaveInteger(udg_PathMotionTable,udg_PathMotionRow,6,GetHandleId(u))
 call Preload("PATHMETA patrol_motion row="+I2S(udg_PathMotionRow)+" tick="+I2S(udg_PathProbeTick)+" case="+I2S(i)+" label="+label)
 set udg_PathMotionRow=udg_PathMotionRow+1
 set u=null
endfunction
function PathMotionBlock takes real x0, real y0, real x1, real y1 returns nothing
 local real x=x0
 local real y
 loop
  exitwhen x>x1
  set y=y0
  loop
   exitwhen y>y1
   call SetTerrainPathable(x,y,PATHING_TYPE_WALKABILITY,false)
   set y=y+32.0
  endloop
  set x=x+32.0
 endloop
endfunction
function PathMotionAdd takes integer i, boolean patrol, real ax, real ay, real bx, real by returns nothing
 set udg_PathMotionUnits[i]=CreateUnit(Player(0),'hfoo',ax,ay,0.0)
 set udg_PathMotionPatrol[i]=patrol
 set udg_PathMotionAX[i]=ax
 set udg_PathMotionAY[i]=ay
 set udg_PathMotionBX[i]=bx
 set udg_PathMotionBY[i]=by
 set udg_PathMotionTowardB[i]=true
 call SetUnitAcquireRange(udg_PathMotionUnits[i],0.0)
 call PathMotionRecord(i,"initial")
endfunction
function PathProbeTick takes nothing returns nothing
 local integer i=0
 local unit u
 set udg_PathProbeTick=udg_PathProbeTick+1
 loop
  exitwhen i==10
  set u=udg_PathMotionUnits[i]
  if udg_PathProbeTick==1 then
   if udg_PathMotionPatrol[i] then
    call IssuePointOrder(u,"patrol",udg_PathMotionBX[i],udg_PathMotionBY[i])
    call PathMotionRecord(i,"patrol")
   else
    call IssuePointOrder(u,"move",udg_PathMotionBX[i],udg_PathMotionBY[i])
    call PathMotionRecord(i,"move")
   endif
  elseif not udg_PathMotionPatrol[i] and GetUnitCurrentOrder(u)==0 then
   set udg_PathMotionTowardB[i]=not udg_PathMotionTowardB[i]
   if udg_PathMotionTowardB[i] then
    call IssuePointOrder(u,"move",udg_PathMotionBX[i],udg_PathMotionBY[i])
   else
    call IssuePointOrder(u,"move",udg_PathMotionAX[i],udg_PathMotionAY[i])
   endif
   call PathMotionRecord(i,"reissue")
  endif
  call PathMotionRecord(i,"sample")
  set i=i+1
 endloop
 set u=null
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label=patrol_motion")
 if udg_PathProbeTick==300 then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_patrol_motion")
 set udg_PathMotionTable=InitHashtable()
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 // Detour walls for lanes 2 and 3; enclosed endpoints for lanes 8 and 9.
 call PathMotionBlock(608.0,608.0,672.0,928.0)
 call PathMotionBlock(608.0,1056.0,672.0,1376.0)
 call PathMotionBlock(1344.0,1040.0,1440.0,1136.0)
 call PathMotionBlock(1600.0,1040.0,1696.0,1136.0)
 call PathMotionAdd(0,false,272.0,256.0,1008.0,256.0)
 call PathMotionAdd(1,true,272.0,448.0,1008.0,448.0)
 call PathMotionAdd(2,false,272.0,768.0,1008.0,768.0)
 call PathMotionAdd(3,true,272.0,1216.0,1008.0,1216.0)
 call PathMotionAdd(4,true,272.0,1600.0,1008.0,1600.0)
 call PathMotionAdd(5,true,1008.0,1600.0,272.0,1600.0)
 call PathMotionAdd(6,false,272.0,1792.0,1008.0,1792.0)
 call PathMotionAdd(7,false,1008.0,1792.0,272.0,1792.0)
 call PathMotionAdd(8,true,1392.0,320.0,1392.0,1088.0)
 call PathMotionAdd(9,false,1648.0,320.0,1648.0,1088.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1008.0)
 call Preload("PATHTRACE tick=0 label=start_patrol_motion")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
