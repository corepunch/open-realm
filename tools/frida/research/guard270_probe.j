globals
 unit array udg_G270Actor
 unit array udg_G270Target
 timer udg_G270Timer=null
 integer udg_G270Tick=0
endglobals
function G270Mark takes string label returns nothing
 local integer i=0
 loop
  exitwhen i==5
  call Preload("G270 tick="+I2S(udg_G270Tick)+" label="+label+" lane="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_G270Actor[i]))+" x="+R2S(GetUnitX(udg_G270Actor[i]))+" y="+R2S(GetUnitY(udg_G270Actor[i])))
  set i=i+1
 endloop
endfunction
function G270Tick takes nothing returns nothing
 local integer i=0
 set udg_G270Tick=udg_G270Tick+1
 if udg_G270Tick==1 then
  call G270Mark("start")
 elseif udg_G270Tick==4 then
  call G270Mark("owner-before")
  loop
   exitwhen i==5
   call SetUnitOwner(udg_G270Target[i],Player(1),false)
   set i=i+1
  endloop
  call G270Mark("owner-after")
 elseif udg_G270Tick==5 then
  call G270Mark("next")
 elseif udg_G270Tick==8 then
  call G270Mark("complete")
  call PreloadGenEnd("guard270.txt")
  call PauseTimer(udg_G270Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 local real dx=300.
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 loop
  exitwhen i==5
  set dx=300.
  if i==1 then
   set dx=128.
  endif
  if i==4 then
   set udg_G270Actor[i]=CreateUnit(Player(0),'hG27',512.,128.+384.*I2R(i),0.)
  else
   set udg_G270Actor[i]=CreateUnit(Player(0),'hfoo',512.,128.+384.*I2R(i),0.)
  endif
  set udg_G270Target[i]=CreateUnit(Player(0),'hfoo',512.+dx,128.+384.*I2R(i),180.)
  call SetUnitInvulnerable(udg_G270Actor[i],true)
  call SetUnitAcquireRange(udg_G270Actor[i],400.)
  call SetUnitAcquireRange(udg_G270Target[i],0.)
  if i<2 or i==4 then
   call UnitRemoveAbility(udg_G270Actor[i],'Amov')
  elseif i==3 then
   call SetUnitMoveSpeed(udg_G270Actor[i],0.)
  endif
  set i=i+1
 endloop
 set udg_G270Timer=CreateTimer()
 call TimerStart(udg_G270Timer,.1,true,function G270Tick)
endfunction
