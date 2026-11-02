globals
 timer udg_B352Timer=null
 integer udg_B352Tick=0
 integer udg_B352Case=0
 unit udg_B352Unit=null
endglobals
function B352Mark takes string label returns nothing
 call Preload("B352 tick="+I2S(udg_B352Tick)+" label="+label)
endfunction
function B352Value takes real a,real b returns nothing
 call B352Mark("case="+I2S(udg_B352Case)+" a="+R2S(a)+" b="+R2S(b))
 call B352Mark("add="+R2S(a+b))
 call B352Mark("sub="+R2S(a-b))
 call B352Mark("mul="+R2S(a*b))
 if b!=0. then
  call B352Mark("div="+R2S(a/b))
 endif
 call B352Mark("sqrt="+R2S(SquareRoot(a)))
 call B352Mark("r2i="+I2S(R2I(a)))
 set a=a+b
 set b=a*b
 call B352Mark("retained a="+R2S(a)+" b="+R2S(b))
 set udg_B352Case=udg_B352Case+1
endfunction
function B352Tick takes nothing returns nothing
 set udg_B352Tick=udg_B352Tick+1
 if udg_B352Tick==1 then
  call B352Mark("start")
 endif
 if udg_B352Tick==2 then
  call B352Value(-0.,0.)
  call B352Value(0.001,0.5)
  call B352Value(-0.001,-0.5)
  call B352Value(16777217.,16777219.)
  call B352Value(-16777217.,-16777219.)
  call B352Value(0.99609375,-0.99609375)
  call B352Value(S2R("10000000000"),S2R("0.00001"))
  call B352Value(S2R("-10000000000"),S2R("-0.00001"))
  call B352Value(I2R(2147483647),I2R(-2147483647))
  call B352Value(I2R(-2147483648),I2R(1073741824))
 endif
 if udg_B352Tick==3 then
  set udg_B352Unit=CreateUnit(Player(0),'hfoo',768.,768.,0.)
  call UnitRemoveAbility(udg_B352Unit,'Aatk')
  call SetUnitFacingTimed(udg_B352Unit,135.,0.25)
 endif
 if udg_B352Tick==10 then
  call IssuePointOrder(udg_B352Unit,"move",1152.125,1024.0625)
 endif
 if udg_B352Tick==20 then
  call SetUnitMoveSpeed(udg_B352Unit,173.25)
 endif
 if udg_B352Tick==30 then
  call SetUnitFacing(udg_B352Unit,225.)
 endif
 if udg_B352Tick==40 then
  call IssueImmediateOrder(udg_B352Unit,"stop")
 endif
 if udg_B352Tick==50 then
  call IssuePointOrder(udg_B352Unit,"move",512.125,768.0625)
 endif
 if udg_B352Tick==80 then
  call B352Mark("complete")
  call PreloadGenEnd("basic352.txt")
  call PauseTimer(udg_B352Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0.)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_B352Timer=CreateTimer()
 call TimerStart(udg_B352Timer,0.1,true,function B352Tick)
endfunction
