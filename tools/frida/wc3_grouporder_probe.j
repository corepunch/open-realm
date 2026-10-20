globals
 hashtable udg_JourneyTable=null
 integer udg_PathProbeTick=0
 timer udg_PathProbeTimer=null
 integer udg_GroupOrderRow=0
 integer udg_GroupOrderSlot=0
endglobals
// Unit-group order probe: make_wc3_journey_map.py (spec "probe": "grouporder") fills the
// cases. Each case creates its types in order, adds them with GroupAddUnit and exports the
// ForGroup enumeration as creation indices (row = case, columns = enumeration slots).
function GroupOrderEnum takes nothing returns nothing
 call SaveInteger(udg_JourneyTable,udg_GroupOrderRow,udg_GroupOrderSlot,LoadInteger(udg_JourneyTable,1000000+GetHandleId(GetEnumUnit()),0))
 set udg_GroupOrderSlot=udg_GroupOrderSlot+1
endfunction
function GroupOrderAdd takes group g, integer kind, integer index returns nothing
 local unit u=CreateUnit(Player(0),kind,-2048.0+128.0*index,-2048.0+256.0*udg_GroupOrderRow,270.0)
 call SaveInteger(udg_JourneyTable,1000000+GetHandleId(u),0,index)
 call GroupAddUnit(g,u)
 set u=null
endfunction
function GroupOrderCase takes group g, integer count returns nothing
 set udg_GroupOrderSlot=0
 call ForGroup(g,function GroupOrderEnum)
 call Preload("PATHMETA ints first="+I2S(udg_GroupOrderRow)+" count=1 cols="+I2S(count))
 call DestroyGroup(g)
 set udg_GroupOrderRow=udg_GroupOrderRow+1
endfunction
function GroupOrderCases takes nothing returns nothing
 local group g
@CASES@
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  call GroupOrderCases()
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_grouporder")
 set udg_JourneyTable=InitHashtable()
 call SetPlayerController(Player(0),MAP_CONTROL_USER)
 call Preload("PATHTRACE tick=0 label=start_journey")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
