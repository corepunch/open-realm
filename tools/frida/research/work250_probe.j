globals
 unit array u250
 timer t250=null
 integer tick250=0
endglobals
function Ordered250 takes nothing returns boolean
 call Preload("P250 tick="+I2S(tick250)+" label=issued unit="+I2S(GetUnitUserData(GetTriggerUnit()))+" head="+I2S(GetUnitCurrentOrder(GetTriggerUnit()))+" event="+I2S(GetIssuedOrderId())+" x="+R2S(GetOrderPointX())+" y="+R2S(GetOrderPointY()))
 return true
endfunction
function Tick250 takes nothing returns nothing
 set tick250=tick250+1
 call Preload("P250 tick="+I2S(tick250)+" order="+I2S(GetUnitCurrentOrder(u250[0]))+" other="+I2S(GetUnitCurrentOrder(u250[2])))
 if tick250==5 then
  call IssuePointOrder(u250[0],"move",2000.0,1500.0)
 endif
 if tick250==240 then
  call Preload("P250 tick=240 label=complete")
  call PreloadGenEnd("rs-selected250.txt")
  call PauseTimer(t250)
 endif
endfunction
function main takes nothing returns nothing
 local trigger listener=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set u250[0]=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
 set u250[1]=CreateUnit(Player(3),'hdes',608.0,512.0,0.0)
 set u250[2]=CreateUnit(Player(3),'hgry',512.0,800.0,0.0)
 set u250[3]=CreateUnit(Player(3),'hfoo',608.0,800.0,0.0)
 call SetUnitUserData(u250[0],0)
 call SetUnitUserData(u250[1],1)
 call SetUnitUserData(u250[2],2)
 call SetUnitUserData(u250[3],3)
 call TriggerRegisterPlayerUnitEvent(listener,Player(3),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddCondition(listener,Condition(function Ordered250))
 call ClearSelection()
 call SelectUnit(u250[0],true)
 call SelectUnit(u250[1],true)
 call SelectUnit(u250[2],true)
 call SelectUnit(u250[3],true)
 call SetCameraPosition(640.0,700.0)
 call Preload("P250 tick=0 label=start player="+I2S(GetPlayerId(GetLocalPlayer())))
 set t250=CreateTimer()
 call TimerStart(t250,0.1,true,function Tick250)
endfunction
