globals
 unit array u241
 timer t241=null
 integer tick241=0
endglobals
function Tick241 takes nothing returns nothing
 set tick241=tick241+1
 call Preload("P241 tick="+I2S(tick241)+" order="+I2S(GetUnitCurrentOrder(u241[0]))+" other="+I2S(GetUnitCurrentOrder(u241[2])))
 if tick241==30 then
  call IssueImmediateOrder(u241[0],"stop")
  call IssueImmediateOrder(u241[3],"stop")
 endif
 if tick241==80 then
  call Preload("P241 tick=80 label=complete")
  call PreloadGenEnd("rs-selected241b.txt")
  call PauseTimer(t241)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u241[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u241[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u241[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u241[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u241[0],true)
 call SelectUnit(u241[1],true)
 call SelectUnit(u241[2],true)
 call SelectUnit(u241[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P241 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t241=CreateTimer()
 call TimerStart(t241,0.1,true,function Tick241)
endfunction
