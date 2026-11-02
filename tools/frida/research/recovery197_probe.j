globals
 timer udg_R197Timer=null
 integer udg_R197Tick=0
 unit udg_R197Unit=null
endglobals
function R197Mark takes string label returns nothing
 call Preload("R197 tick="+I2S(udg_R197Tick)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_R197Unit))+" x="+R2SW(GetUnitX(udg_R197Unit),1,4)+" y="+R2SW(GetUnitY(udg_R197Unit),1,4))
endfunction
function R197Tick takes nothing returns nothing
 set udg_R197Tick=udg_R197Tick+1
 if udg_R197Tick==1 then
  call R197Mark("start_file")
  call PreloadGenEnd("rs-recovery197-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_R197Tick==5 then
  call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
  call EnableUserControl(true)
  call ShowInterface(true,0)
  call ClearSelection()
  call SelectUnit(udg_R197Unit,true)
  call R197Mark("selected")
 endif
 if udg_R197Tick==50 then
  call IssuePointOrder(udg_R197Unit,"move",1008,1040)
  call R197Mark("blocked-order")
 endif
 call R197Mark("sample")
 if udg_R197Tick==250 then
  call R197Mark("complete")
  call PauseTimer(udg_R197Timer)
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_R197Unit=CreateUnit(GetLocalPlayer(),'hfoo',272,304,90)
 call SetUnitMoveSpeed(udg_R197Unit,270)
 call SetCameraPosition(600,850)
 call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1600,0)
 call R197Mark("begin-setup")
 set udg_R197Timer=CreateTimer()
 call TimerStart(udg_R197Timer,0.1,true,function R197Tick)
endfunction
