globals
 unit array u248
 timer t248=null
 integer tick248=0
endglobals
function Ordered248 takes nothing returns boolean
 call Preload("P248 tick="+I2S(tick248)+" label=issued unit="+I2S(GetUnitUserData(GetTriggerUnit())))
 return true
endfunction
function Tick248 takes nothing returns nothing
 set tick248=tick248+1
 call Preload("P248 tick="+I2S(tick248)+" order="+I2S(GetUnitCurrentOrder(u248[0]))+" other="+I2S(GetUnitCurrentOrder(u248[2])))
 if tick248==5 then
  call IssuePointOrder(u248[0],"move",2000.0,1500.0)
 endif
 if tick248==240 then
  call Preload("P248 tick=240 label=complete")
  call PreloadGenEnd("rs-selected248.txt")
  call PauseTimer(t248)
 endif
endfunction
function main takes nothing returns nothing
 local trigger listener=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u248[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u248[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u248[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u248[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call SetUnitUserData(u248[0],0)
 call SetUnitUserData(u248[1],1)
 call SetUnitUserData(u248[2],2)
 call SetUnitUserData(u248[3],3)
 call TriggerRegisterPlayerUnitEvent(listener,Player(3),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddCondition(listener,Condition(function Ordered248))
 call ClearSelection()
 call SelectUnit(u248[0],true)
 call SelectUnit(u248[1],true)
 call SelectUnit(u248[2],true)
 call SelectUnit(u248[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P248 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t248=CreateTimer()
 call TimerStart(t248,0.1,true,function Tick248)
endfunction
