globals
 hashtable udg_JourneyTable=null
 unit array udg_JourneyUnits
 integer udg_JourneyCount=0
 integer udg_JourneyRow=0
 integer udg_PathProbeTick=0
 timer udg_PathProbeTimer=null
endglobals
// Long-journey probe: make_wc3_journey_map.py fills the units, the order timeline and
// the sample cadence from a journey spec. Every sampled tick exports, per subject,
// tick/case/order/GetUnitFacing/GetUnitX/GetUnitY and then one PATHMETA marker naming
// the exported row range, so original and engine replays compare word for word.
function JourneyAdd takes integer owner, integer kind, real x, real y, real facing returns nothing
 set udg_JourneyUnits[udg_JourneyCount]=CreateUnit(Player(owner),kind,x,y,facing)
 call SetUnitAcquireRange(udg_JourneyUnits[udg_JourneyCount],0.0)
 set udg_JourneyCount=udg_JourneyCount+1
endfunction
function JourneySample takes nothing returns nothing
 local integer i=0
 local integer first=udg_JourneyRow
 local unit u
 loop
  exitwhen i==udg_JourneyCount
  set u=udg_JourneyUnits[i]
  call SaveInteger(udg_JourneyTable,udg_JourneyRow,0,udg_PathProbeTick)
  call SaveInteger(udg_JourneyTable,udg_JourneyRow,1,i)
  call SaveInteger(udg_JourneyTable,udg_JourneyRow,2,GetUnitCurrentOrder(u))
  call SaveReal(udg_JourneyTable,udg_JourneyRow,3,GetUnitFacing(u))
  call SaveReal(udg_JourneyTable,udg_JourneyRow,4,GetUnitX(u))
  call SaveReal(udg_JourneyTable,udg_JourneyRow,5,GetUnitY(u))
  set udg_JourneyRow=udg_JourneyRow+1
  set i=i+1
 endloop
 set u=null
 call Preload("PATHMETA journey tick="+I2S(udg_PathProbeTick)+" first="+I2S(first)+" count="+I2S(udg_JourneyCount))
endfunction
// Subjects first..last (inclusive) receive one public group point order.
function JourneyGroupPoint takes integer first, integer last, string order, real x, real y returns nothing
 local group g=CreateGroup()
 local integer i=first
 loop
  exitwhen i>last
  call GroupAddUnit(g,udg_JourneyUnits[i])
  set i=i+1
 endloop
 call GroupPointOrder(g,order,x,y)
 call DestroyGroup(g)
 set g=null
endfunction
function JourneyPoint takes integer i, string order, real x, real y returns nothing
 call IssuePointOrder(udg_JourneyUnits[i],order,x,y)
endfunction
function JourneyImmediate takes integer i, string order returns nothing
 call IssueImmediateOrder(udg_JourneyUnits[i],order)
endfunction
function JourneyTarget takes integer i, string order, integer target returns nothing
 call IssueTargetOrder(udg_JourneyUnits[i],order,udg_JourneyUnits[target])
endfunction
function JourneyTimeline takes nothing returns nothing
@TIMELINE@
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 call JourneyTimeline()
 if udg_PathProbeTick-(udg_PathProbeTick/@SAMPLE_EVERY@)*@SAMPLE_EVERY@==0 then
  call JourneySample()
 endif
 if udg_PathProbeTick==@TICKS@ then
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_journey")
 set udg_JourneyTable=InitHashtable()
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
@UNITS@
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(@CAMERA_X@,@CAMERA_Y@)
 call JourneySample()
 call Preload("PATHTRACE tick=0 label=start_journey")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
