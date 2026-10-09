globals
 unit array udg_P192U
 integer udg_P192Tick=0
endglobals
function P192Mark takes string label returns nothing
 call Preload("P192 tick="+I2S(udg_P192Tick)+" label="+label)
endfunction
function P192Orders takes nothing returns nothing
 local integer i=0
 local real x
 local real y
 loop
  exitwhen i>=129
  set x=256.0+I2R(ModuloInteger(i,16))*96.0
  set y=256.0+I2R(i/16)*128.0
  if IssuePointOrder(udg_P192U[i],"move",x+24.0,y) then
   call Preload("P192 tick="+I2S(udg_P192Tick)+" label=accepted i="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_P192U[i])))
  else
   call Preload("P192 tick="+I2S(udg_P192Tick)+" label=rejected i="+I2S(i))
  endif
  set i=i+1
 endloop
endfunction
function P192Step takes nothing returns nothing
 local integer i=0
 set udg_P192Tick=udg_P192Tick+1
 if udg_P192Tick==1 then
  call P192Mark("begin-setup")
  loop
   exitwhen i>=129
   set udg_P192U[i]=CreateUnit(Player(0),'hfoo',256.0+I2R(ModuloInteger(i,16))*96.0,256.0+I2R(i/16)*128.0,0.0)
   call SetUnitAcquireRange(udg_P192U[i],0.0)
   set i=i+1
  endloop
  call P192Mark("created")
 elseif udg_P192Tick==10 or udg_P192Tick==30 then
  call P192Mark("move")
  call P192Orders()
  call P192Mark("move-end")
 elseif udg_P192Tick==20 or udg_P192Tick==40 then
  call P192Mark("stop")
  loop
   exitwhen i>=129
   call IssueImmediateOrder(udg_P192U[i],"stop")
   set i=i+1
  endloop
  call P192Mark("stop-end")
 elseif udg_P192Tick==50 then
  call P192Mark("remove")
  loop
   exitwhen i>=129
   call RemoveUnit(udg_P192U[i])
   set udg_P192U[i]=null
   set i=i+1
  endloop
 elseif udg_P192Tick==60 then
  call P192Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call DestroyTimer(GetExpiredTimer())
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call TimerStart(CreateTimer(),0.1,true,function P192Step)
endfunction
