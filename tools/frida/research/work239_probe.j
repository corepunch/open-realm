globals
 unit array u239
 timer t239=null
 integer tick239=0
endglobals
function Tick239 takes nothing returns nothing
 set tick239=tick239+1
 call Preload("P239 tick="+I2S(tick239)+" order="+I2S(GetUnitCurrentOrder(u239[0]))+" other="+I2S(GetUnitCurrentOrder(u239[2])))
 if tick239==80 then
  call Preload("P239 tick=80 label=complete")
  call PreloadGenEnd("rs-selected239.txt")
  call PauseTimer(t239)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u239[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u239[1]=CreateUnit(Player(3),'hfoo',608.0,512.0,0.0)
 set u239[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u239[3]=CreateUnit(Player(3),'hgry',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u239[0],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P239 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t239=CreateTimer()
 call TimerStart(t239,0.1,true,function Tick239)
endfunction
