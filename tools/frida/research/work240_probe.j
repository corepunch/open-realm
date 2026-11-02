globals
 unit array u240
 timer t240=null
 integer tick240=0
endglobals
function Tick240 takes nothing returns nothing
 set tick240=tick240+1
 call Preload("P240 tick="+I2S(tick240)+" order="+I2S(GetUnitCurrentOrder(u240[0]))+" other="+I2S(GetUnitCurrentOrder(u240[2])))
 if tick240==80 then
  call Preload("P240 tick=80 label=complete")
  call PreloadGenEnd("rs-selected240.txt")
  call PauseTimer(t240)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u240[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u240[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u240[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u240[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u240[0],true)
 call SelectUnit(u240[1],true)
 call SelectUnit(u240[2],true)
 call SelectUnit(u240[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P240 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t240=CreateTimer()
 call TimerStart(t240,0.1,true,function Tick240)
endfunction
