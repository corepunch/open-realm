globals
 unit array u273
 integer tick273=0
 timer timer273=null
endglobals
function Tick273 takes nothing returns nothing
 local integer i=0
 set tick273=tick273+1
 if tick273==1 then
  call StartCampaignAI(Player(0),"Scripts\\capscope273.ai")
 endif
 loop
  exitwhen i==3
  call Preload("P273 tick="+I2S(tick273)+" unit="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(u273[i]))+" x="+R2S(GetUnitX(u273[i]))+" y="+R2S(GetUnitY(u273[i])))
  set i=i+1
 endloop
 if tick273==60 then
  call Preload("P273 tick=60 label=complete")
  call PreloadGenEnd("rs-capscope273.txt")
  call PauseTimer(timer273)
 endif
endfunction
function main takes nothing returns nothing
 local integer i=0
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 loop
  exitwhen i==3
  set u273[i]=CreateUnit(Player(0),'hfoo',512.0+I2R(i)*96.0,512.0,0.0)
  call SetUnitAcquireRange(u273[i],0.0)
  set i=i+1
 endloop
 call Preload("P273 tick=0 label=start")
 set timer273=CreateTimer()
 call TimerStart(timer273,0.1,true,function Tick273)
endfunction
