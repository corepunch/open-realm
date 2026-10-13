globals
 timer udg_N313Timer=null
 integer udg_N313Tick=0
 integer udg_N313Scene=-1
 unit udg_N313Unit=null
 real udg_N313Source=0
endglobals
function N313Scalar takes real value returns string
 if value!=value then
  return "nan"
 elseif value>10000000 then
  return "high"
 elseif value<-10000000 then
  return "low"
 endif
 return R2SW(value,1,4)
endfunction
function N313Mark takes string label returns nothing
 call Preload("N313 tick="+I2S(udg_N313Tick)+" scene="+I2S(udg_N313Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_N313Unit))+" x="+N313Scalar(GetUnitX(udg_N313Unit))+" y="+N313Scalar(GetUnitY(udg_N313Unit))+" source="+N313Scalar(udg_N313Source))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function N313Test takes nothing returns nothing
 set udg_N313Source=0
 call N313Mark("begin-setup")
 set udg_N313Unit=CreateUnit(Player(0),'hOAA'+udg_N313Scene,768,512,0)
 call SetUnitAcquireRange(udg_N313Unit,0)
 call N313Mark("created")
 set udg_N313Source=GetUnitDefaultFlyHeight(udg_N313Unit)
 call N313Mark("source")
 call SetUnitX(udg_N313Unit,udg_N313Source)
 call N313Mark("axis-x")
 call SetUnitPosition(udg_N313Unit,768,512)
 call N313Mark("restore-x")
 call SetUnitY(udg_N313Unit,udg_N313Source)
 call N313Mark("axis-y")
 call SetUnitPosition(udg_N313Unit,768,512)
 call N313Mark("restore-y")
 call SetUnitPosition(udg_N313Unit,udg_N313Source,udg_N313Source)
 call N313Mark("placed")
 call SetUnitPathing(udg_N313Unit,false)
 call SetUnitPosition(udg_N313Unit,udg_N313Source,udg_N313Source)
 call N313Mark("queryzero")
 call RemoveUnit(udg_N313Unit)
 set udg_N313Unit=null
 call N313Mark("end-cleanup")
endfunction
function N313Tick takes nothing returns nothing
 set udg_N313Tick=udg_N313Tick+1
 if udg_N313Tick==1 then
  call N313Mark("start")
  call PreloadGenEnd("nonfinite313-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_N313Tick>=20 and udg_N313Tick<=43 then
  set udg_N313Scene=udg_N313Tick-20
  call N313Test()
 endif
 if udg_N313Tick==44 then
  call N313Mark("complete")
  call PauseTimer(udg_N313Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_N313Timer=CreateTimer()
 call TimerStart(udg_N313Timer,0.1,true,function N313Tick)
endfunction
