globals
 timer udg_R211Timer=null
 integer udg_R211Tick=0
 unit array udg_R211U
endglobals
function R211Mark takes string label returns nothing
 local integer i=0
 local string s="R211 tick="+I2S(udg_R211Tick)+" label="+label
 call Preload(s)
 loop
  exitwhen i==3
  call Preload("R211 tick="+I2S(udg_R211Tick)+" label=unit"+I2S(i)+" x="+R2S(GetUnitX(udg_R211U[i]))+" y="+R2S(GetUnitY(udg_R211U[i]))+" f="+R2S(GetUnitFacing(udg_R211U[i]))+" o="+I2S(GetUnitCurrentOrder(udg_R211U[i])))
  set i=i+1
 endloop
endfunction
function R211Tick takes nothing returns nothing
 set udg_R211Tick=udg_R211Tick+1
 if udg_R211Tick==2 then
  call IssueImmediateOrder(udg_R211U[0],"unroot")
  call IssueImmediateOrder(udg_R211U[1],"unroot")
  call IssueImmediateOrder(udg_R211U[2],"unroot")
  call R211Mark("unroot")
 endif
 if udg_R211Tick==50 then
  call SetUnitFacing(udg_R211U[0],0)
  call SetUnitFacing(udg_R211U[1],180)
  call SetUnitFacing(udg_R211U[2],0)
  call R211Mark("before_root")
  call IssuePointOrder(udg_R211U[0],"root",512,512)
  call IssuePointOrder(udg_R211U[1],"root",1024,512)
  call IssuePointOrder(udg_R211U[2],"root",1536,1024)
  call R211Mark("after_root")
 endif
 if udg_R211Tick==53 then
  call IssueImmediateOrder(udg_R211U[1],"stop")
  call R211Mark("stop_turn")
 endif
 call R211Mark("sample")
 if udg_R211Tick==260 then
  call R211Mark("complete")
  call PauseTimer(udg_R211Timer)
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
 call R211Mark("start")
 loop
  exitwhen i==3
  set udg_R211U[i]=CreateUnit(Player(0),'etol',512+512*i,512,0)
  call SetUnitAcquireRange(udg_R211U[i],0)
  set i=i+1
 endloop
 call R211Mark("created")
 set udg_R211Timer=CreateTimer()
 call TimerStart(udg_R211Timer,0.1,true,function R211Tick)
endfunction
