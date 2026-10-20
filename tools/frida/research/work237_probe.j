globals
 unit array u237
 timer t237=null
 integer tick237=0
endglobals
function Tick237 takes nothing returns nothing
 set tick237=tick237+1
 call Preload("P237 tick="+I2S(tick237)+" order="+I2S(GetUnitCurrentOrder(u237[0]))+" other="+I2S(GetUnitCurrentOrder(u237[2])))
 if tick237==80 then
  call Preload("P237 tick=80 label=complete")
  call PreloadGenEnd("rs-selected237.txt")
  call PauseTimer(t237)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u237[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u237[1]=CreateUnit(Player(3),'hfoo',608.0,512.0,0.0)
 set u237[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u237[3]=CreateUnit(Player(3),'hgry',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u237[0],true)
 call SelectUnit(u237[1],true)
 call SelectUnit(u237[2],true)
 call SelectUnit(u237[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P237 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t237=CreateTimer()
 call TimerStart(t237,0.1,true,function Tick237)
endfunction
