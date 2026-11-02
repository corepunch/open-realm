globals
 timer udg_N312Timer=null
 integer udg_N312Tick=0
 integer udg_N312Scene=-1
 unit udg_N312Unit=null
endglobals
function N312Mark takes string label returns nothing
 call Preload("N312 tick="+I2S(udg_N312Tick)+" scene="+I2S(udg_N312Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_N312Unit))+" x="+R2SW(GetUnitX(udg_N312Unit),1,4)+" y="+R2SW(GetUnitY(udg_N312Unit),1,4))
endfunction
function N312Test takes nothing returns nothing
 local real x=768
 local real y=512
 local integer side=ModuloInteger(udg_N312Scene,4)
 if side==1 then
  set x=-16384
  set y=-512
 elseif side==2 then
  set x=32768
  set y=2560
 elseif side==3 then
  set x=-0.0001
  set y=2048.0002
 endif
 call N312Mark("begin-setup")
 set udg_N312Unit=CreateUnit(Player(0),'hNAA'+udg_N312Scene/4,x,y,0)
 call SetUnitAcquireRange(udg_N312Unit,0)
 call N312Mark("created")
 call SetUnitPosition(udg_N312Unit,x,y)
 call N312Mark("placed")
 call SetUnitScale(udg_N312Unit,-2,-2,-2)
 call N312Mark("scale-negative")
 call SetUnitPathing(udg_N312Unit,false)
 call SetUnitPosition(udg_N312Unit,x,y)
 call N312Mark("queryzero")
 call SetUnitX(udg_N312Unit,x)
 call SetUnitY(udg_N312Unit,y)
 call N312Mark("axis-setters")
 call RemoveUnit(udg_N312Unit)
 set udg_N312Unit=null
 call N312Mark("end-cleanup")
endfunction
function N312Tick takes nothing returns nothing
 set udg_N312Tick=udg_N312Tick+1
 if udg_N312Tick==1 then
  call N312Mark("start")
  call PreloadGenEnd("numeric312-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_N312Tick>=20 and udg_N312Tick<=339 then
  set udg_N312Scene=udg_N312Tick-20
  call N312Test()
 endif
 if udg_N312Tick==340 then
  call N312Mark("complete")
  call PauseTimer(udg_N312Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_N312Timer=CreateTimer()
 call TimerStart(udg_N312Timer,0.1,true,function N312Tick)
endfunction
