globals
 unit array u233
 group g233=null
 timer t233=null
 integer tick233=0
endglobals
function Start233 takes integer mode returns nothing
 local integer i=0
 if g233!=null then
  call DestroyGroup(g233)
  call RemoveUnit(u233[0])
  call RemoveUnit(u233[1])
 endif
 set g233=CreateGroup()
 set u233[0]=CreateUnit(Player(0),'hgry',768.0,512.0,0.0)
 set u233[1]=CreateUnit(Player(0),'hgry',512.0,512.0,0.0)
 if mode>0 then
  call UnitAddAbility(u233[0],'AS33')
 endif
 if mode>1 then
  call UnitAddAbility(u233[1],'AS33')
 endif
 call GroupAddUnit(g233,u233[0])
 call GroupAddUnit(g233,u233[1])
 call Preload("P233 tick="+I2S(tick233)+" begin="+I2S(mode))
endfunction
function Order233 takes integer mode returns nothing
 if GroupPointOrder(g233,"move",1536.0,512.0) then
  call Preload("P233 tick="+I2S(tick233)+" ordered="+I2S(mode))
 endif
endfunction
function Tick233 takes nothing returns nothing
 set tick233=tick233+1
 if tick233==1 then
  call Start233(0)
 elseif tick233==3 then
  call Order233(0)
 elseif tick233==20 then
  call Start233(1)
 elseif tick233==22 then
  call Order233(1)
 elseif tick233==40 then
  call Start233(2)
 elseif tick233==42 then
  call Order233(2)
 elseif tick233==60 then
  call Preload("P233 tick=60 label=complete")
  call PreloadGenEnd("rs-source233.txt")
  call PauseTimer(t233)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(768.0,512.0)
 call Preload("P233 tick=0 label=start")
 set t233=CreateTimer()
 call TimerStart(t233,0.1,true,function Tick233)
endfunction
