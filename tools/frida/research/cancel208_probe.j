globals
 timer udg_C208Timer=null
 integer udg_C208Tick=0
 unit array udg_C208U
endglobals
function C208Mark takes string label returns nothing
 local integer i=0
 local string s="C208 tick="+I2S(udg_C208Tick)+" label="+label
 loop
  exitwhen i==8
  set s=s+" u"+I2S(i)+"="+R2S(GetUnitX(udg_C208U[i]))+","+R2S(GetUnitY(udg_C208U[i]))+","+R2S(GetUnitFacing(udg_C208U[i]))+","+I2S(GetUnitCurrentOrder(udg_C208U[i]))
  set i=i+1
 endloop
 call Preload(s)
endfunction
function C208Tick takes nothing returns nothing
 set udg_C208Tick=udg_C208Tick+1
 if udg_C208Tick==4 then
  call IssuePointOrder(udg_C208U[2],"move",1536,640)
  call IssuePointOrder(udg_C208U[3],"move",1536,832)
  call IssuePointOrder(udg_C208U[4],"move",1536,1024)
 endif
 if udg_C208Tick==5 then
  call SetUnitFacingTimed(udg_C208U[0],270,1.2)
  call SetUnitFacingTimed(udg_C208U[1],270,1.2)
  call SetUnitFacingTimed(udg_C208U[2],270,1.2)
  call SetUnitFacingTimed(udg_C208U[5],270,1.2)
  call SetUnitFacingTimed(udg_C208U[6],90,1.2)
  call C208Mark("before_stop_natural_turn")
  call IssueImmediateOrder(udg_C208U[3],"stop")
  call IssuePointOrder(udg_C208U[4],"move",128,1024)
  call C208Mark("after_stop_natural_turn")
 endif
 if udg_C208Tick==6 then
  call C208Mark("before_cancel_angular")
  call IssueImmediateOrder(udg_C208U[0],"stop")
  call IssueImmediateOrder(udg_C208U[1],"holdposition")
  call IssueImmediateOrder(udg_C208U[2],"stop")
  call IssuePointOrder(udg_C208U[5],"move",1536,1216)
  call IssueImmediateOrder(udg_C208U[7],"stop")
  call C208Mark("after_cancel_angular")
 endif
 call C208Mark("sample")
 if udg_C208Tick==80 then
  call C208Mark("complete")
  call PauseTimer(udg_C208Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 call C208Mark("start")
 loop
  exitwhen i==8
  set udg_C208U[i]=CreateUnit(Player(0),'hF00',512,256+192*i,0)
  call SetUnitAcquireRange(udg_C208U[i],0)
  set i=i+1
 endloop
 call SetUnitFacing(udg_C208U[2],180)
 call SetUnitFacing(udg_C208U[3],180)
 call SetUnitFacing(udg_C208U[4],180)
 call C208Mark("created")
 set udg_C208Timer=CreateTimer()
 call TimerStart(udg_C208Timer,0.1,true,function C208Tick)
endfunction
