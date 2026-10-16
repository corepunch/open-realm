globals
 timer udg_P309Timer=null
 integer udg_P309Tick=0
 integer udg_P309Scene=-1
 unit udg_P309Unit=null
endglobals
function P309Mark takes string label returns nothing
 call Preload("P309 tick="+I2S(udg_P309Tick)+" scene="+I2S(udg_P309Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_P309Unit))+" x="+R2SW(GetUnitX(udg_P309Unit),1,4)+" y="+R2SW(GetUnitY(udg_P309Unit),1,4))
endfunction
function P309Test takes nothing returns nothing
 local real x=1024
 local real y=1024
 local integer side=ModuloInteger(udg_P309Scene,6)
 if side==0 then
  set x=-512
 elseif side==1 then
  set x=2560
 elseif side==2 then
  set y=-512
 elseif side==3 then
  set y=2560
 elseif side==4 then
  set x=-512
  set y=2560
 else
  set x=2048
  set y=2048
 endif
 call P309Mark("begin-setup")
 set udg_P309Unit=CreateUnit(Player(0),'hQ0A'+udg_P309Scene/6,x,y,0)
 call SetUnitAcquireRange(udg_P309Unit,0)
 call P309Mark("created")
 call SetUnitPosition(udg_P309Unit,x,y)
 call P309Mark("placed")
 call SetUnitPathing(udg_P309Unit,false)
 call SetUnitPosition(udg_P309Unit,x,y)
 call P309Mark("queryzero")
 call RemoveUnit(udg_P309Unit)
 set udg_P309Unit=null
 call P309Mark("end-cleanup")
endfunction
function P309Tick takes nothing returns nothing
 set udg_P309Tick=udg_P309Tick+1
 if udg_P309Tick==1 then
  call P309Mark("start")
  call PreloadGenEnd("place309-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_P309Tick>=20 and udg_P309Tick<=91 then
  set udg_P309Scene=udg_P309Tick-20
  call P309Test()
 endif
 if udg_P309Tick==92 then
  call P309Mark("complete")
  call PauseTimer(udg_P309Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P309Timer=CreateTimer()
 call TimerStart(udg_P309Timer,0.1,true,function P309Tick)
endfunction
