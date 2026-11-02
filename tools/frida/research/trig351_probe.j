globals
 timer udg_T351Timer=null
 integer udg_T351Tick=0
 integer udg_T351Case=0
 unit udg_T351Unit=null
endglobals
function T351Mark takes string label returns nothing
 call Preload("T351 tick="+I2S(udg_T351Tick)+" label="+label)
endfunction
function T351Value takes real a,real b returns nothing
 call T351Mark("case="+I2S(udg_T351Case)+" a="+R2S(a)+" b="+R2S(b))
 call T351Mark("sin="+R2S(Sin(a)))
 call T351Mark("cos="+R2S(Cos(a)))
 call T351Mark("tan="+R2S(Tan(a)))
 call T351Mark("asin="+R2S(Asin(a)))
 call T351Mark("acos="+R2S(Acos(a)))
 call T351Mark("atan="+R2S(Atan(a)))
 call T351Mark("atan2="+R2S(Atan2(a,b)))
 set udg_T351Case=udg_T351Case+1
endfunction
function T351Tick takes nothing returns nothing
 set udg_T351Tick=udg_T351Tick+1
 if udg_T351Tick==1 then
  call T351Mark("start")
 endif
 if udg_T351Tick==2 then
  call T351Value(-1.,-1.)
  call T351Value(-0.99609375,1.)
  call T351Value(-0.5,-0.5)
  call T351Value(-0.001,0.)
  call T351Value(-0.,-0.)
  call T351Value(0.,0.)
  call T351Value(0.001,-0.001)
  call T351Value(0.5,-0.5)
  call T351Value(0.99609375,1.)
  call T351Value(1.,-1.)
  call T351Value(3.14159265,0.5)
  call T351Value(-3.14159265,-0.5)
 endif
 if udg_T351Tick==3 then
  set udg_T351Unit=CreateUnit(Player(0),'hfoo',768.,768.,0.)
  call UnitRemoveAbility(udg_T351Unit,'Aatk')
  call SetUnitFacingTimed(udg_T351Unit,135.,0.25)
 endif
 if udg_T351Tick==10 then
  call IssuePointOrder(udg_T351Unit,"move",1152.,1024.)
 endif
 if udg_T351Tick==30 then
  call SetUnitFacing(udg_T351Unit,225.)
 endif
 if udg_T351Tick==80 then
  call T351Mark("complete")
  call PreloadGenEnd("trig351.txt")
  call PauseTimer(udg_T351Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0.)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_T351Timer=CreateTimer()
 call TimerStart(udg_T351Timer,0.1,true,function T351Tick)
endfunction
