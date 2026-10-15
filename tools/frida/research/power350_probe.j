globals
 timer udg_P350Timer=null
 timer udg_P350Heart=null
 integer udg_P350Tick=0
 integer udg_P350Beat=0
 integer udg_P350Case=@CASE@
 real udg_P350Result=0
endglobals
function P350Mark takes string label returns nothing
 call Preload("P350 tick="+I2S(udg_P350Tick)+" beat="+I2S(udg_P350Beat)+" label="+label)
endfunction
function P350Heart takes nothing returns nothing
 set udg_P350Beat=udg_P350Beat+1
 call P350Mark("heartbeat")
endfunction
function P350Tick takes nothing returns nothing
 set udg_P350Tick=udg_P350Tick+1
 if udg_P350Tick==1 then
  call P350Mark("start")
  call PreloadGenEnd("power350-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_P350Tick==20 then
  call P350Mark("before")
  call PreloadGenEnd("power350-before.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  if udg_P350Case==0 then
   set udg_P350Result=Pow(2.0,I2R(1073741824)*2.0)
  elseif udg_P350Case==1 then
   set udg_P350Result=Pow(2.0,-I2R(1000000000))
  elseif udg_P350Case==2 then
   set udg_P350Result=Pow(0.5,-I2R(1000000000))
  else
   set udg_P350Result=Pow(2.0,I2R(1073741824))
  endif
  call P350Mark("after")
 endif
 if udg_P350Tick==30 then
  call P350Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_P350Timer)
  call PauseTimer(udg_P350Heart)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P350Timer=CreateTimer()
 set udg_P350Heart=CreateTimer()
 call TimerStart(udg_P350Heart,0.15,true,function P350Heart)
 call TimerStart(udg_P350Timer,0.1,true,function P350Tick)
endfunction
