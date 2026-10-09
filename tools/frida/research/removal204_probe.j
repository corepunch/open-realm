// Public removal within an issued-point callback, followed by real policy refreshes.
globals
 timer udg_R204Timer=null
 integer udg_R204Tick=0
 unit udg_R204A=null
 unit udg_R204B=null
 unit udg_R204C=null
endglobals
function R204Mark takes string label returns nothing
 call Preload("R204 tick="+I2S(udg_R204Tick)+" label="+label+" a="+I2S(GetUnitTypeId(udg_R204A))+":"+I2S(GetUnitCurrentOrder(udg_R204A))+" b="+I2S(GetUnitTypeId(udg_R204B))+" c="+I2S(GetUnitTypeId(udg_R204C)))
endfunction
function R204Remove takes nothing returns nothing
 if GetIssuedOrderId()!=OrderId("move") or GetUnitTypeId(udg_R204A)==0 then
  return
 endif
 call R204Mark("nested_before")
 call RemoveUnit(udg_R204A)
 call R204Mark("nested_removed")
 call SetUnitOwner(udg_R204A,Player(1),false)
 call R204Mark("nested_owner")
 call PauseUnit(udg_R204A,true)
 call R204Mark("nested_paused")
 call PauseUnit(udg_R204A,false)
 call R204Mark("nested_resumed")
endfunction
function R204Tick takes nothing returns nothing
 set udg_R204Tick=udg_R204Tick+1
 if udg_R204Tick==5 then
  call R204Mark("issue_before")
  call IssuePointOrder(udg_R204A,"move",1024,512)
  call R204Mark("issue_after")
 endif
 if udg_R204Tick==10 then
  call R204Mark("ordinary_before")
  call RemoveUnit(udg_R204B)
  call R204Mark("ordinary_after")
 endif
 if udg_R204Tick==15 then
  call PauseUnit(udg_R204C,true)
  call R204Mark("paused_before")
  call RemoveUnit(udg_R204C)
  call R204Mark("paused_after")
 endif
 call R204Mark("sample")
 if udg_R204Tick==30 then
  call R204Mark("complete")
  call PauseTimer(udg_R204Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 call R204Mark("start")
 set udg_R204A=CreateUnit(Player(0),'hREM',512,512,0)
 set udg_R204B=CreateUnit(Player(0),'hREM',768,512,0)
 set udg_R204C=CreateUnit(Player(0),'hREM',768,768,0)
 call SetUnitAcquireRange(udg_R204A,0)
 call SetUnitAcquireRange(udg_R204B,0)
 call SetUnitAcquireRange(udg_R204C,0)
 call TriggerRegisterUnitEvent(t,udg_R204A,EVENT_UNIT_ISSUED_POINT_ORDER)
 call TriggerAddAction(t,function R204Remove)
 call R204Mark("created")
 set udg_R204Timer=CreateTimer()
 call TimerStart(udg_R204Timer,0.1,true,function R204Tick)
 set t=null
endfunction
