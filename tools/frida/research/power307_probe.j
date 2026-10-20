globals
 timer udg_P307Timer=null
 timer udg_P307Heart=null
 integer udg_P307Tick=0
 integer udg_P307Beat=0
 real udg_P307Result=0
endglobals
function P307Mark takes string label returns nothing
 call Preload("P307 tick="+I2S(udg_P307Tick)+" beat="+I2S(udg_P307Beat)+" label="+label)
endfunction
function P307Heart takes nothing returns nothing
 set udg_P307Beat=udg_P307Beat+1
 call P307Mark("heartbeat")
endfunction
function P307Tick takes nothing returns nothing
 set udg_P307Tick=udg_P307Tick+1
 if udg_P307Tick==1 then
  call P307Mark("start")
  call PreloadGenEnd("power307-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_P307Tick==20 then
  call P307Mark("before")
  call PreloadGenEnd("power307-before.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  set udg_P307Result=Pow(2.0,I2R(1073741824)*@FACTOR@)
  call P307Mark("after")
 endif
 if udg_P307Tick==30 then
  call P307Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_P307Timer)
  call PauseTimer(udg_P307Heart)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P307Timer=CreateTimer()
 set udg_P307Heart=CreateTimer()
 call TimerStart(udg_P307Heart,0.15,true,function P307Heart)
 call TimerStart(udg_P307Timer,0.1,true,function P307Tick)
endfunction
