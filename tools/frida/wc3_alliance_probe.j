globals
 hashtable udg_JourneyTable=null
 integer udg_PathProbeTick=0
 timer udg_PathProbeTimer=null
endglobals
// Diplomacy probe: make_wc3_journey_map.py (spec "probe": "alliances"). Row phase*512+i*16+j
// holds GetPlayerAlliance(Player(i),Player(j),type) for the ten alliance types; row
// phase*512+256+i holds the controller, slot state, allied-victory state and team. Phase 0 is the map's configured state,
// phase 1 follows the journey probes' SetPlayerController(Player(0),MAP_CONTROL_USER).
function AllianceControl takes mapcontrol c returns integer
 if c==MAP_CONTROL_USER then
  return 0
 elseif c==MAP_CONTROL_COMPUTER then
  return 1
 elseif c==MAP_CONTROL_RESCUABLE then
  return 2
 elseif c==MAP_CONTROL_NEUTRAL then
  return 3
 elseif c==MAP_CONTROL_CREEP then
  return 4
 endif
 return 5
endfunction
function AllianceSlot takes playerslotstate s returns integer
 if s==PLAYER_SLOT_STATE_EMPTY then
  return 0
 elseif s==PLAYER_SLOT_STATE_PLAYING then
  return 1
 endif
 return 2
endfunction
function AllianceDump takes integer phase returns nothing
 local integer i=0
 local integer j
 local integer t
 loop
  exitwhen i==16
  set j=0
  loop
   exitwhen j==16
   set t=0
   loop
    exitwhen t==10
    if GetPlayerAlliance(Player(i),Player(j),ConvertAllianceType(t)) then
     call SaveInteger(udg_JourneyTable,phase*512+i*16+j,t,1)
    else
     call SaveInteger(udg_JourneyTable,phase*512+i*16+j,t,0)
    endif
    set t=t+1
   endloop
   set j=j+1
  endloop
  call SaveInteger(udg_JourneyTable,phase*512+256+i,0,AllianceControl(GetPlayerController(Player(i))))
  call SaveInteger(udg_JourneyTable,phase*512+256+i,1,AllianceSlot(GetPlayerSlotState(Player(i))))
  call SaveInteger(udg_JourneyTable,phase*512+256+i,2,GetPlayerState(Player(i),PLAYER_STATE_ALLIED_VICTORY))
  call SaveInteger(udg_JourneyTable,phase*512+256+i,3,GetPlayerTeam(Player(i)))
  set i=i+1
 endloop
 call Preload("PATHMETA ints first="+I2S(phase*512)+" count=256 cols=10")
 call Preload("PATHMETA ints first="+I2S(phase*512+256)+" count=16 cols=4")
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  call AllianceDump(0)
  call SetPlayerController(Player(0),MAP_CONTROL_USER)
  call AllianceDump(1)
  call Preload("PATHMETA complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_alliances")
 set udg_JourneyTable=InitHashtable()
 call Preload("PATHTRACE tick=0 label=start_journey")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
