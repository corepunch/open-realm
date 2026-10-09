globals
 timer udg_Q208Timer=null
 integer udg_Q208Tick=0
 unit array udg_Q208U
endglobals
function Q208Mark takes string label returns nothing
 local integer i=0
 local string s="Q208 tick="+I2S(udg_Q208Tick)+" label="+label
 loop
  exitwhen i==96
  if i==50 or i==52 or i==94 or i==95 then
   set s=s+" u"+I2S(i)+"="+R2S(GetUnitX(udg_Q208U[i]))+","+R2S(GetUnitY(udg_Q208U[i]))+","+I2S(GetUnitCurrentOrder(udg_Q208U[i]))
  endif
  set i=i+1
 endloop
 call Preload(s)
endfunction
function Q208Tick takes nothing returns nothing
 local integer i=0
 set udg_Q208Tick=udg_Q208Tick+1
 if udg_Q208Tick==10 then
  call Q208Mark("wave_before")
  loop
   exitwhen i==96
   call IssuePointOrder(udg_Q208U[i],"move",1728,128+I2R(i/8)*64)
   set i=i+1
  endloop
  call Q208Mark("wave_after")
 elseif udg_Q208Tick==11 then
  call Q208Mark("pending_before")
  call IssueImmediateOrder(udg_Q208U[52],"stop")
  call Q208Mark("pending_stopped")
  call IssuePointOrder(udg_Q208U[50],"move",256,128+I2R(50/8)*64)
  call Q208Mark("pending_replaced")
 elseif udg_Q208Tick==12 then
  call Q208Mark("pending_next")
 elseif udg_Q208Tick==15 then
  call Q208Mark("travel_before")
  call IssueImmediateOrder(udg_Q208U[94],"stop")
  call Q208Mark("travel_stopped")
  call IssuePointOrder(udg_Q208U[95],"move",256,128+I2R(95/8)*64)
  call Q208Mark("travel_replaced")
 elseif udg_Q208Tick==16 then
  call Q208Mark("travel_next")
 endif
 call Q208Mark("sample")
 if udg_Q208Tick==45 then
  set i=0
  loop
   exitwhen i==96
   call IssueImmediateOrder(udg_Q208U[i],"stop")
   set i=i+1
  endloop
  call Q208Mark("complete")
  call PauseTimer(udg_Q208Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,true)
 call PreloadGenClear()
 call PreloadGenStart()
 call Q208Mark("start")
 loop
  exitwhen i==96
  set udg_Q208U[i]=CreateUnit(Player(ModuloInteger(i,2)),'hfoo',128+I2R(ModuloInteger(i,8))*64,128+I2R(i/8)*64,0)
  call SetUnitMoveSpeed(udg_Q208U[i],100)
  call SetUnitAcquireRange(udg_Q208U[i],0)
  set i=i+1
 endloop
 set udg_Q208Timer=CreateTimer()
 call TimerStart(udg_Q208Timer,0.1,true,function Q208Tick)
endfunction
