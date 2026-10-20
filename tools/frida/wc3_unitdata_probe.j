globals
 hashtable udg_JourneyTable=null
 integer udg_JourneyRow=0
 integer udg_PathProbeTick=0
 timer udg_PathProbeTimer=null
endglobals
// Authored unit scalar probe: make_wc3_journey_map.py (spec "probe": "unitdata") fills
// the type list. Each type is created, its public default/current scalars are exported as raw
// words, and the unit is removed before the next type.
function UnitDataRecord takes integer kind returns nothing
 local unit u=CreateUnit(Player(0),kind,0.0,0.0,0.0)
 call SaveInteger(udg_JourneyTable,udg_JourneyRow,0,kind)
 call SaveReal(udg_JourneyTable,udg_JourneyRow,1,GetUnitDefaultTurnSpeed(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,2,GetUnitDefaultPropWindow(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,3,GetUnitDefaultMoveSpeed(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,4,GetUnitDefaultAcquireRange(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,5,GetUnitDefaultFlyHeight(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,6,GetUnitTurnSpeed(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,7,GetUnitPropWindow(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,8,GetUnitMoveSpeed(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,9,GetUnitAcquireRange(u))
 call SaveReal(udg_JourneyTable,udg_JourneyRow,10,GetUnitFlyHeight(u))
 call Preload("PATHMETA unitdata row="+I2S(udg_JourneyRow))
 set udg_JourneyRow=udg_JourneyRow+1
 call RemoveUnit(u)
 set u=null
endfunction
function UnitDataTypes takes nothing returns nothing
@TYPES@
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  call UnitDataTypes()
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_unitdata")
 set udg_JourneyTable=InitHashtable()
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call Preload("PATHTRACE tick=0 label=start_journey")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
