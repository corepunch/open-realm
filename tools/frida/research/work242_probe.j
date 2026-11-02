globals
 unit array u242
 timer t242=null
 integer tick242=0
endglobals
function Tick242 takes nothing returns nothing
 set tick242=tick242+1
 call Preload("P242 tick="+I2S(tick242)+" order="+I2S(GetUnitCurrentOrder(u242[0]))+" other="+I2S(GetUnitCurrentOrder(u242[2])))
 if tick242==5 then
  call IssuePointOrder(u242[0],"move",3500.0,3500.0)
  call IssuePointOrder(u242[1],"move",3500.0,3500.0)
  call IssuePointOrder(u242[2],"move",3500.0,3500.0)
  call IssuePointOrder(u242[3],"move",3500.0,3500.0)
 endif
 if tick242==80 then
  call Preload("P242 tick=80 label=complete")
  call PreloadGenEnd("rs-selected242.txt")
  call PauseTimer(t242)
 endif
endfunction
function main takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u242[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u242[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u242[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u242[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call ClearSelection()
 call SelectUnit(u242[0],true)
 call SelectUnit(u242[1],true)
 call SelectUnit(u242[2],true)
 call SelectUnit(u242[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P242 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t242=CreateTimer()
 call TimerStart(t242,0.1,true,function Tick242)
endfunction
