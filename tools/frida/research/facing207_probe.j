globals
 timer udg_F207Timer=null
 integer udg_F207Tick=0
 unit array udg_F207U
endglobals
function F207Mark takes string label returns nothing
 local integer i=0
 local string s="F207 tick="+I2S(udg_F207Tick)+" label="+label
 loop
  exitwhen i==5
  set s=s+" u"+I2S(i)+"="+R2S(GetUnitX(udg_F207U[i]))+","+R2S(GetUnitY(udg_F207U[i]))+","+R2S(GetUnitFacing(udg_F207U[i]))+","+I2S(GetUnitCurrentOrder(udg_F207U[i]))
  set i=i+1
 endloop
 call Preload(s)
endfunction
function F207Tick takes nothing returns nothing
 set udg_F207Tick=udg_F207Tick+1
 if udg_F207Tick==3 then
  call PauseUnit(udg_F207U[4],true)
 endif
 if udg_F207Tick==4 then
  call IssuePointOrder(udg_F207U[1],"move",1536,512)
 endif
 if udg_F207Tick==5 then
  call F207Mark("before_timed")
  call SetUnitFacingTimed(udg_F207U[0],90,1.2)
  call SetUnitFacingTimed(udg_F207U[1],90,1.2)
  call SetUnitFacingTimed(udg_F207U[2],180,0.1)
  call SetUnitFacingTimed(udg_F207U[3],45,0.3001)
  call SetUnitFacingTimed(udg_F207U[4],90,1.2)
  call F207Mark("after_timed")
 endif
 if udg_F207Tick==10 then
  call PauseUnit(udg_F207U[4],false)
 endif
 if udg_F207Tick==15 then
  call SetUnitFacingTimed(udg_F207U[2],270,0.100001)
  call F207Mark("threshold_positive")
 endif
 if udg_F207Tick==25 then
  call SetUnitFacingTimed(udg_F207U[0],270,0.6)
  call F207Mark("timed_replacement")
 endif
 if udg_F207Tick==28 then
  call IssuePointOrder(udg_F207U[0],"move",1536,256)
  call F207Mark("move_replacement")
 endif
 if udg_F207Tick==35 then
  call SetUnitFacingTimed(udg_F207U[1],180,0)
  call F207Mark("immediate_replacement")
 endif
 call F207Mark("sample")
 if udg_F207Tick==80 then
  call F207Mark("complete")
  call PauseTimer(udg_F207Timer)
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
 call F207Mark("start")
 loop
  exitwhen i==5
  set udg_F207U[i]=CreateUnit(Player(0),'hF00',512,256+256*i,0)
  call SetUnitAcquireRange(udg_F207U[i],0)
  set i=i+1
 endloop
 call SetUnitFacing(udg_F207U[3],315)
 call F207Mark("created")
 set udg_F207Timer=CreateTimer()
 call TimerStart(udg_F207Timer,0.1,true,function F207Tick)
endfunction
