globals
 timer udg_S314Timer=null
 integer udg_S314Tick=0
 integer udg_S314Scene=-1
 unit udg_S314Unit=null
 real udg_S314Source=0
endglobals
function S314Scalar takes real value returns string
 if value!=value then
  return "nan"
 elseif value>10000000 then
  return "high"
 elseif value<-10000000 then
  return "low"
 endif
 return R2SW(value,1,4)
endfunction
function S314Mark takes string label returns nothing
 call Preload("S314 tick="+I2S(udg_S314Tick)+" scene="+I2S(udg_S314Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S314Unit))+" x="+S314Scalar(GetUnitX(udg_S314Unit))+" y="+S314Scalar(GetUnitY(udg_S314Unit))+" speed="+S314Scalar(GetUnitMoveSpeed(udg_S314Unit))+" default="+S314Scalar(GetUnitDefaultMoveSpeed(udg_S314Unit))+" source="+S314Scalar(udg_S314Source))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function S314Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_S314Tick+1-20,32)
 local unit source=null
 set udg_S314Tick=udg_S314Tick+1
 if udg_S314Tick==1 then
  call S314Mark("start")
  call PreloadGenEnd("speed314-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_S314Tick==20+32*@COUNT@ then
  call S314Mark("complete")
  call PauseTimer(udg_S314Timer)
 elseif udg_S314Tick>=20 then
  if age==0 then
   set udg_S314Scene=@FIRST@+(udg_S314Tick-20)/32
   call S314Mark("begin-setup")
   set source=CreateUnit(Player(0),'hSAA'+udg_S314Scene,1800,1800,0)
   set udg_S314Source=GetUnitDefaultFlyHeight(source)
   call RemoveUnit(source)
   set source=null
   set udg_S314Unit=CreateUnit(Player(0),'hTAA',768,512,0)
   call SetUnitAcquireRange(udg_S314Unit,0)
   call S314Mark("created")
  elseif age==1 then
   call SetUnitMoveSpeed(udg_S314Unit,udg_S314Source)
   call S314Mark("set-exceptional")
  elseif age==2 then
   call IssuePointOrder(udg_S314Unit,"move",1280,768)
   call S314Mark("move")
  elseif age==15 then
   call SetUnitMoveSpeed(udg_S314Unit,270)
   call S314Mark("restore")
  elseif age==25 then
   call IssueImmediateOrder(udg_S314Unit,"stop")
   call S314Mark("stop")
  elseif age==30 then
   call RemoveUnit(udg_S314Unit)
   set udg_S314Unit=null
   call S314Mark("end-cleanup")
  elseif age>2 and age<30 then
   call S314Mark("sample")
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_S314Timer=CreateTimer()
 call TimerStart(udg_S314Timer,0.1,true,function S314Tick)
endfunction
