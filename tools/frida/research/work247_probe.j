globals
 unit udg_W247Unit=null
 timer udg_W247Timer=null
 integer udg_W247Tick=0
 integer udg_W247Scene=0
endglobals
function W247Mark takes string label returns nothing
 call Preload("W247 tick="+I2S(udg_W247Tick)+" scene="+I2S(udg_W247Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W247Unit))+" x="+R2S(GetUnitX(udg_W247Unit))+" y="+R2S(GetUnitY(udg_W247Unit)))
endfunction
function W247Terrain takes boolean enabled returns nothing
 local integer x=8
 local integer y=8
 loop
  exitwhen y>32
  set x=8
  loop
   exitwhen x>32
   if udg_W247Scene==2 or (x>=19 and x<=21 and y>=19 and y<=21) then
    call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,enabled)
   endif
   set x=x+1
  endloop
  set y=y+1
 endloop
endfunction
function W247Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_W247Tick,6)
 if phase==0 then
  set udg_W247Unit=CreateUnit(Player(0),'hfoo',272,272,0)
  call SetUnitAcquireRange(udg_W247Unit,0)
  call SetUnitPathing(udg_W247Unit,false)
  call SetUnitPosition(udg_W247Unit,656,656)
  if udg_W247Scene!=3 then
   call SetUnitPathing(udg_W247Unit,true)
  endif
  if udg_W247Scene==1 or udg_W247Scene==2 then
   call W247Terrain(false)
  endif
 elseif phase==1 then
  call W247Mark("before")
  call IssueImmediateOrder(udg_W247Unit,"stop")
  call W247Mark("after")
 elseif phase==4 then
  call W247Mark("settled")
  call RemoveUnit(udg_W247Unit)
  call W247Terrain(true)
 elseif phase==5 then
  set udg_W247Scene=udg_W247Scene+1
  if udg_W247Scene==4 then
   call W247Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_W247Timer)
  endif
 endif
 set udg_W247Tick=udg_W247Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 set udg_W247Timer=CreateTimer()
 call TimerStart(udg_W247Timer,0.1,true,function W247Tick)
 call Preload("W247 tick=0 scene=0 label=start order=0")
endfunction
