globals
 unit udg_W246F=null
 unit udg_W246T=null
 timer udg_W246Timer=null
 integer udg_W246Tick=0
endglobals
function W246Mark takes string label returns nothing
 call Preload("W246 tick="+I2S(udg_W246Tick)+" scene=0 label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W246F))+" x="+R2S(GetUnitX(udg_W246F))+" y="+R2S(GetUnitY(udg_W246F))+" tx="+R2S(GetUnitX(udg_W246T))+" ty="+R2S(GetUnitY(udg_W246T)))
endfunction
function W246Tick takes nothing returns nothing
 if udg_W246Tick==0 then
  set udg_W246F=CreateUnit(Player(0),'hfoo',-7800,-7800,0)
  set udg_W246T=CreateUnit(Player(0),'hfoo',-7350,-7500,30)
  call SetUnitAcquireRange(udg_W246F,0)
  call SetUnitAcquireRange(udg_W246T,0)
  call SetUnitMoveSpeed(udg_W246T,150)
  call W246Mark("begin")
 elseif udg_W246Tick==1 then
  call IssuePointOrder(udg_W246T,"move",-6550,-6600)
 elseif udg_W246Tick==4 then
  if IssueTargetOrder(udg_W246F,"smart",udg_W246T) then
   call W246Mark("accepted")
  else
   call W246Mark("rejected")
  endif
 elseif udg_W246Tick==70 then
  call IssueImmediateOrder(udg_W246F,"stop")
  call IssueImmediateOrder(udg_W246T,"stop")
  call W246Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_W246Timer)
 endif
 call W246Mark("sample")
 set udg_W246Tick=udg_W246Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_W246Timer=CreateTimer()
 call TimerStart(udg_W246Timer,0.1,true,function W246Tick)
 call Preload("W246 tick=0 scene=0 label=start order=0")
endfunction
