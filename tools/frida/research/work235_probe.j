globals
 unit array u235
 group g235=null
 timer t235=null
 integer tick235=0
endglobals
function Start235 takes integer mode returns nothing
 local integer i=0
 if g235!=null then
  call DestroyGroup(g235)
  loop
   exitwhen i==3
   call RemoveUnit(u235[i])
   set i=i+1
  endloop
 endif
 set g235=CreateGroup()
 set u235[0]=CreateUnit(Player(0),'hgry',256.0,512.0,0.0)
 set u235[1]=CreateUnit(Player(0),'hgry',2880.0,512.0,0.0)
 set u235[2]=CreateUnit(Player(0),'hgry',1536.0,512.0,0.0)
 if mode==2 then
  call SetUnitX(u235[1],2816.0)
 endif
 set i=0
 loop
  exitwhen i==3
  if mode>0 then
   call UnitAddAbility(u235[i],'AS33')
  endif
  call GroupAddUnit(g235,u235[i])
  set i=i+1
 endloop
 call Preload("P235 tick="+I2S(tick235)+" begin="+I2S(mode))
endfunction
function Order235 takes integer mode returns nothing
 if GroupPointOrder(g235,"move",512.0,1536.0) then
  call Preload("P235 tick="+I2S(tick235)+" ordered="+I2S(mode))
 endif
endfunction
function Tick235 takes nothing returns nothing
 set tick235=tick235+1
 if tick235==1 then
  call Start235(0)
 elseif tick235==3 then
  call Order235(0)
 elseif tick235==20 then
  call Start235(1)
 elseif tick235==22 then
  call Order235(1)
 elseif tick235==40 then
  call Start235(2)
 elseif tick235==42 then
  call Order235(2)
 elseif tick235==60 then
  call Preload("P235 tick=60 label=complete")
  call PreloadGenEnd("rs-partition235.txt")
  call PauseTimer(t235)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(768.0,512.0)
 call Preload("P235 tick=0 label=start")
 set t235=CreateTimer()
 call TimerStart(t235,0.1,true,function Tick235)
endfunction
