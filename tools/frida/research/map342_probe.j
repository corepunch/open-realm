globals
 unit udg_M342Unit=null
 trigger udg_M342Trigger=null
 region udg_M342Region=null
 rect udg_M342Rect=null
 timer udg_M342Timer=null
 integer udg_M342Tick=0
 integer udg_M342Scene=0
 boolean udg_M342Nested=false
endglobals
function M342Mark takes string label returns nothing
 call Preload("M342 tick="+I2S(udg_M342Tick)+" scene="+I2S(udg_M342Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_M342Unit))+" x="+R2S(GetUnitX(udg_M342Unit))+" y="+R2S(GetUnitY(udg_M342Unit)))
endfunction
function M342Terrain takes boolean enabled returns nothing
 local integer x=19
 local integer y=19
 loop
  exitwhen y>21
  set x=19
  loop
   exitwhen x>21
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,enabled)
   set x=x+1
  endloop
  set y=y+1
 endloop
endfunction
function M342Filter takes nothing returns boolean
 if GetFilterUnit()!=udg_M342Unit then
  return false
 endif
 call M342Mark("filter-enter")
 if not udg_M342Nested then
  set udg_M342Nested=true
  if udg_M342Scene==1 then
   call IssueImmediateOrder(udg_M342Unit,"stop")
  elseif udg_M342Scene==2 then
   call IssuePointOrder(udg_M342Unit,"move",1200,592)
  elseif udg_M342Scene==3 then
   call PauseUnit(udg_M342Unit,true)
  elseif udg_M342Scene==4 then
   call KillUnit(udg_M342Unit)
  elseif udg_M342Scene==5 then
   call RemoveUnit(udg_M342Unit)
  elseif udg_M342Scene==6 then
   call SetUnitPosition(udg_M342Unit,1000,1000)
  elseif udg_M342Scene==7 then
   call M342Terrain(true)
  endif
 endif
 call M342Mark("filter-exit")
 return true
endfunction
function M342Action takes nothing returns nothing
 call M342Mark("action")
endfunction
function M342Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_M342Tick,10)
 if phase==0 then
  set udg_M342Nested=false
  call M342Terrain(true)
  set udg_M342Unit=CreateUnit(Player(0),'hfoo',656,656,0)
  call SetUnitAcquireRange(udg_M342Unit,0)
  set udg_M342Rect=Rect(608,576,640,608)
  set udg_M342Region=CreateRegion()
  call RegionAddRect(udg_M342Region,udg_M342Rect)
  set udg_M342Trigger=CreateTrigger()
  call TriggerRegisterEnterRegion(udg_M342Trigger,udg_M342Region,Condition(function M342Filter))
  call TriggerAddAction(udg_M342Trigger,function M342Action)
 elseif phase==1 then
  call M342Terrain(false)
  call M342Mark("before")
  call IssueImmediateOrder(udg_M342Unit,"stop")
  call M342Mark("after")
 elseif phase==4 then
  call M342Mark("settled")
 elseif phase==8 then
  call DestroyTrigger(udg_M342Trigger)
  call RemoveRegion(udg_M342Region)
  call RemoveRect(udg_M342Rect)
  call RemoveUnit(udg_M342Unit)
  set udg_M342Unit=null
  call M342Terrain(true)
  set udg_M342Scene=udg_M342Scene+1
  if udg_M342Scene==8 then
   call M342Mark("complete")
   call PreloadGenEnd("map342.txt")
   call PauseTimer(udg_M342Timer)
  endif
 endif
 set udg_M342Tick=udg_M342Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call M342Mark("start")
 set udg_M342Timer=CreateTimer()
 call TimerStart(udg_M342Timer,.1,true,function M342Tick)
endfunction
