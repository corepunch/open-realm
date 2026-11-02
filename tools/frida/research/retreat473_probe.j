globals
 timer udg_R473Timer=null
 integer udg_R473Tick=0
 unit array udg_R473Units
endglobals
function R473Mark takes string label returns nothing
 local integer i=0
 call Preload("R473 tick="+I2S(udg_R473Tick)+" label="+label)
 loop
  exitwhen i==6
  call Preload("R473 tick="+I2S(udg_R473Tick)+" label=unit u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_R473Units[i]))+" x="+R2SW(GetUnitX(udg_R473Units[i]),1,4)+" y="+R2SW(GetUnitY(udg_R473Units[i]),1,4))
  set i=i+1
 endloop
 call PreloadGenEnd("retreat473.txt")
endfunction
function R473Tick takes nothing returns nothing
 local integer i=0
 set udg_R473Tick=udg_R473Tick+1
 if udg_R473Tick==1 then
  call R473Mark("start")
 elseif udg_R473Tick==10 then
  loop
   exitwhen i==6
   set udg_R473Units[i]=CreateUnit(Player(0),'hfoo',512+I2R(i-(i/4)*4)*96,512+I2R(i/4)*96,0)
   set i=i+1
  endloop
  call R473Mark("created")
 elseif udg_R473Tick==12 then
  call StartCampaignAI(Player(0),"Scripts\\retreat473.ai")
  call R473Mark("ai")
 elseif udg_R473Tick==135 then
  loop
   exitwhen i==4
   call RemoveUnit(udg_R473Units[i])
   set i=i+1
  endloop
  call R473Mark("remove")
 elseif udg_R473Tick==300 then
  call R473Mark("complete")
  call PauseTimer(udg_R473Timer)
 elseif udg_R473Tick>12 then
  call R473Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_R473Timer=CreateTimer()
 call TimerStart(udg_R473Timer,0.10,true,function R473Tick)
endfunction
