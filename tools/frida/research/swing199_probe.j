globals
 timer udg_S199Timer=null
 integer udg_S199Tick=0
 unit array udg_S199U
endglobals
function S199Mark takes string label returns nothing
 local integer i=0
 local string s="S199 tick="+I2S(udg_S199Tick)+" label="+label
 loop
  exitwhen i>5
  set s=s+" u"+I2S(i)+"="+R2S(GetUnitX(udg_S199U[i]))+","+R2S(GetUnitY(udg_S199U[i]))+","+I2S(GetUnitCurrentOrder(udg_S199U[i]))+","+R2S(GetUnitState(udg_S199U[i],UNIT_STATE_LIFE))
  set i=i+1
 endloop
 call Preload(s)
endfunction
function S199Tick takes nothing returns nothing
 set udg_S199Tick=udg_S199Tick+1
 if udg_S199Tick==1 then
  call S199Mark("start_file")
  call PreloadGenEnd("rs-swing199-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_S199Tick==10 then
  call S199Mark("before_attack")
  call IssueTargetOrder(udg_S199U[0],"attack",udg_S199U[4])
  call IssueTargetOrder(udg_S199U[1],"attackonce",udg_S199U[5])
  call IssueTargetOrder(udg_S199U[2],"attack",udg_S199U[5])
  call IssuePointOrder(udg_S199U[3],"attackground",640,1408)
 endif
 if udg_S199Tick==20 then
  call IssuePointOrder(udg_S199U[0],"move",960,256)
  call IssuePointOrder(udg_S199U[1],"move",960,640)
  call IssuePointOrder(udg_S199U[2],"move",960,960)
  call IssuePointOrder(udg_S199U[3],"move",960,1408)
  call S199Mark("move_after_swing")
 endif
 call S199Mark("sample")
 if udg_S199Tick==80 then
  call S199Mark("complete")
  call PauseTimer(udg_S199Timer)
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
 set udg_S199U[0]=CreateUnit(Player(0),'hfoo',256,256,0)
 set udg_S199U[1]=CreateUnit(Player(0),'hrif',256,640,0)
 set udg_S199U[2]=CreateUnit(Player(0),'hmtm',256,960,0)
 set udg_S199U[3]=CreateUnit(Player(0),'hmtm',256,1408,0)
 set udg_S199U[4]=CreateUnit(Player(1),'hfoo',320,256,180)
 set udg_S199U[5]=CreateUnit(Player(1),'hfoo',640,800,180)
 loop
  exitwhen i>5
  call SetUnitAcquireRange(udg_S199U[i],0)
  if i>3 then
   call SetUnitState(udg_S199U[i],UNIT_STATE_MAX_LIFE,10000)
   call SetUnitState(udg_S199U[i],UNIT_STATE_LIFE,10000)
   call PauseUnit(udg_S199U[i],true)
  endif
  set i=i+1
 endloop
 call S199Mark("start")
 set udg_S199Timer=CreateTimer()
 call TimerStart(udg_S199Timer,0.1,true,function S199Tick)
endfunction
