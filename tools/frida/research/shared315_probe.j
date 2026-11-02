globals
 timer udg_S315Timer=null
 integer udg_S315Tick=0
 integer udg_S315Scene=@FIRST@
 unit array udg_S315Units
 real udg_S315Source=0
endglobals
function S315Mark takes string label returns nothing
 local integer i=0
 call Preload("S315 tick="+I2S(udg_S315Tick)+" scene="+I2S(udg_S315Scene)+" label="+label)
 loop
  exitwhen i==3
  call Preload("S315 tick="+I2S(udg_S315Tick)+" scene="+I2S(udg_S315Scene)+" label=unit u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_S315Units[i]))+" x="+R2SW(GetUnitX(udg_S315Units[i]),1,4)+" y="+R2SW(GetUnitY(udg_S315Units[i]),1,4)+" speed="+R2SW(GetUnitMoveSpeed(udg_S315Units[i]),1,4))
  set i=i+1
 endloop
 call PreloadGenEnd("@OUTPUT@")
endfunction
function S315Tick takes nothing returns nothing
 local integer i=0
 local unit source=null
 set udg_S315Tick=udg_S315Tick+1
 if udg_S315Tick==1 then
  call S315Mark("start")
  call PreloadGenEnd("shared315-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_S315Tick==20 then
  call S315Mark("begin-setup")
  set source=CreateUnit(Player(0),'hSAA'+udg_S315Scene,1800,1800,0)
  set udg_S315Source=GetUnitDefaultFlyHeight(source)
  call RemoveUnit(source)
  set source=null
  loop
   exitwhen i==3
   set udg_S315Units[i]=CreateUnit(Player(0),'hTAA',512+I2R(i)*96,512,0)
   call SetUnitAcquireRange(udg_S315Units[i],0)
   set i=i+1
  endloop
  call S315Mark("created")
 elseif udg_S315Tick==21 then
  call SetUnitMoveSpeed(udg_S315Units[0],udg_S315Source)
  call S315Mark("set-exceptional")
 elseif udg_S315Tick==22 then
  call StartCampaignAI(Player(0),"Scripts\\shared315.ai")
  call S315Mark("start-ai")
 elseif udg_S315Tick==60 then
  call SetUnitMoveSpeed(udg_S315Units[0],270)
  call S315Mark("restore")
 elseif udg_S315Tick==90 then
  loop
   exitwhen i==3
   call IssueImmediateOrder(udg_S315Units[i],"stop")
   set i=i+1
  endloop
  call S315Mark("stop")
 elseif udg_S315Tick==110 then
  call S315Mark("complete")
  call PauseTimer(udg_S315Timer)
 elseif udg_S315Tick>22 then
  call S315Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_S315Timer=CreateTimer()
 call TimerStart(udg_S315Timer,0.1,true,function S315Tick)
endfunction
