globals
 timer udg_P308Timer=null
 integer udg_P308Tick=0
 integer udg_P308Scene=-1
 unit udg_P308Unit=null
endglobals
function P308Mark takes string label returns nothing
 call Preload("P308 tick="+I2S(udg_P308Tick)+" scene="+I2S(udg_P308Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_P308Unit))+" x="+R2SW(GetUnitX(udg_P308Unit),1,4)+" y="+R2SW(GetUnitY(udg_P308Unit),1,4))
endfunction
function P308Test takes nothing returns nothing
 local real x=1024
 local real y=1024
 local integer side=ModuloInteger(udg_P308Scene,6)
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
 call P308Mark("begin-setup")
 set udg_P308Unit=CreateUnit(Player(0),'hfoo',288,288,0)
 call SetUnitAcquireRange(udg_P308Unit,0)
 if udg_P308Scene>=6 then
  call SetUnitPathing(udg_P308Unit,false)
 endif
 call P308Mark("created")
 call SetUnitPosition(udg_P308Unit,x,y)
 call P308Mark("placed")
 call RemoveUnit(udg_P308Unit)
 set udg_P308Unit=null
 call P308Mark("end-cleanup")
endfunction
function P308Tick takes nothing returns nothing
 set udg_P308Tick=udg_P308Tick+1
 if udg_P308Tick==1 then
  call P308Mark("start")
  call PreloadGenEnd("place308-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_P308Tick>=20 and udg_P308Tick<=31 then
  set udg_P308Scene=udg_P308Tick-20
  call P308Test()
 endif
 if udg_P308Tick==32 then
  call P308Mark("complete")
  call PauseTimer(udg_P308Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P308Timer=CreateTimer()
 call TimerStart(udg_P308Timer,0.1,true,function P308Tick)
endfunction
