globals
 unit array u243
 timer t243=null
 integer tick243=0
endglobals
function Tick243 takes nothing returns nothing
 set tick243=tick243+1
 call Preload("P243 tick="+I2S(tick243)+" order="+I2S(GetUnitCurrentOrder(u243[0]))+" other="+I2S(GetUnitCurrentOrder(u243[2])))
 if tick243==5 then
  call IssuePointOrder(u243[0],"move",2000.0,1500.0)
 endif
 if tick243==240 then
  call Preload("P243 tick=240 label=complete")
  call PreloadGenEnd("rs-selected243.txt")
  call PauseTimer(t243)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u243[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u243[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u243[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u243[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u243[0],true)
 call SelectUnit(u243[1],true)
 call SelectUnit(u243[2],true)
 call SelectUnit(u243[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P243 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t243=CreateTimer()
 call TimerStart(t243,0.1,true,function Tick243)
endfunction
