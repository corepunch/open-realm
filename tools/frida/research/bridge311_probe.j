globals
 timer udg_B311Timer=null
 integer udg_B311Tick=0
 integer udg_B311Scene=-1
 unit udg_B311Unit=null
 destructable udg_B311Bridge=null
 destructable udg_B311Other=null
endglobals
function B311Mark takes string label returns nothing
 call Preload("B311 tick="+I2S(udg_B311Tick)+" scene="+I2S(udg_B311Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_B311Unit))+" x="+R2SW(GetUnitX(udg_B311Unit),1,4)+" y="+R2SW(GetUnitY(udg_B311Unit),1,4))
endfunction
function B311Test takes nothing returns nothing
 local real x=1024
 local real y=640
 local integer side=ModuloInteger(udg_B311Scene,8)
 if side==1 then
  set x=576
 elseif side==2 then
  set y=192
 elseif side==3 then
  set x=256
  set y=192
 elseif side==4 then
  set x=640
  set y=1856
 elseif side==5 then
  set x=1408
  set y=1856
 elseif side==6 then
  set y=960
 elseif side==7 then
  set y=384
 endif
 call B311Mark("begin-setup")
 if udg_B311Scene>=160 then
  set x=1024
  set y=640
  if udg_B311Scene==160 then
   call KillDestructable(udg_B311Bridge)
  elseif udg_B311Scene==162 then
   call DestructableRestoreLife(udg_B311Bridge,10000,false)
  elseif udg_B311Scene==164 then
   call RemoveDestructable(udg_B311Bridge)
   set udg_B311Bridge=null
  elseif udg_B311Scene==166 then
   set udg_B311Bridge=CreateDestructableZ('LT06',1024,640,128,0,1,0)
  elseif udg_B311Scene==168 then
   set udg_B311Other=CreateDestructableZ('LT06',1024,640,256,0,1,0)
  elseif udg_B311Scene==170 then
   call RemoveDestructable(udg_B311Bridge)
   set udg_B311Bridge=null
  elseif udg_B311Scene==172 then
   call KillDestructable(udg_B311Other)
  elseif udg_B311Scene==174 then
   call DestructableRestoreLife(udg_B311Other,10000,false)
  elseif udg_B311Scene==176 then
   call RemoveDestructable(udg_B311Other)
   set udg_B311Other=null
  endif
  call B311Mark("provider-updated")
 endif
 set udg_B311Unit=CreateUnit(Player(0),'hQ0A'+ModuloInteger(udg_B311Scene/8,20),x,y,0)
 call SetUnitAcquireRange(udg_B311Unit,0)
 call B311Mark("created")
 call SetUnitPosition(udg_B311Unit,x,y)
 call B311Mark("placed")
 call SetUnitPathing(udg_B311Unit,false)
 call SetUnitPosition(udg_B311Unit,x,y)
 call B311Mark("queryzero")
 call RemoveUnit(udg_B311Unit)
 set udg_B311Unit=null
 call B311Mark("end-cleanup")
endfunction
function B311Tick takes nothing returns nothing
 set udg_B311Tick=udg_B311Tick+1
 if udg_B311Tick==1 then
  call B311Mark("start")
  call PreloadGenEnd("bridge311-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_B311Tick>=20 and udg_B311Tick<=197 then
  set udg_B311Scene=udg_B311Tick-20
  call B311Test()
 endif
 if udg_B311Tick==198 then
  call B311Mark("complete")
  call PauseTimer(udg_B311Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function B311FindBridge takes nothing returns nothing
 if GetDestructableTypeId(GetEnumDestructable())=='LT06' then
  set udg_B311Bridge=GetEnumDestructable()
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call EnumDestructablesInRect(Rect(0,0,2048,2048),null,function B311FindBridge)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_B311Timer=CreateTimer()
 call TimerStart(udg_B311Timer,0.1,true,function B311Tick)
endfunction
