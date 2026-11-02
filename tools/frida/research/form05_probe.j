globals
 unit array udg_F05U
 unit udg_F05Gate=null
 timer udg_F05Timer=null
 integer udg_F05Tick=0
endglobals

// FORM-05.1 / FORM-05.2 / FORM-04.2 research probe (new file). Variant @VARIANT@, 0.1-second ticks.
// Six Footman rank clones hF00..hF03 (ranks 0/1/2/3/0/1), speeds 100/100/100/350/350/350, collision 31,
// at x=320, y=832+64*i. Terrain (builder WPM): wall base column x=14 except a two-cell gap y=15..16
// (world 960..1087); closed ring x=25..30/y=3..8 around interior world (1664..1919, 256..511).
// a: actual UI selection Move (owned helper click) at tick 30 to (1600,1024) through the gap.
// b: independently issued public IssuePointOrder for all six at tick 30, same point (control).
// d: Way Gate at (512,1700) -> (1600,1750); UI selection Move at tick 30 to (1750,1800) (warp transition),
//    then at tick 250 a UI selection Move into the closed ring interior (1792,384) (route failure).
// The camera is placed so that client pixel 560/230 is the intended point (Payoff109 calibration:
// camera (512,512) + target distance 1800 mapped 560/230 to world 618.7733/691.2574).

function F05Mark takes string label returns nothing
 local integer i=0
 local string s="F05 tick="+I2S(udg_F05Tick)+" label="+label+" variant=@VARIANT@"
 loop
  exitwhen i>5
  set s=s+" u"+I2S(i)+"="+R2S(GetUnitX(udg_F05U[i]))+","+R2S(GetUnitY(udg_F05U[i]))+","+I2S(GetUnitCurrentOrder(udg_F05U[i]))
  set i=i+1
 endloop
 call Preload(s)
endfunction

function F05Camera takes real x,real y returns nothing
 call SetCameraPosition(x-106.7733,y-179.2574)
 call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
endfunction

function F05Tick takes nothing returns nothing
 local integer i=0
 local string v="@VARIANT@"
 set udg_F05Tick=udg_F05Tick+1
 if udg_F05Tick==1 then
  // Start signal file for observer-free control timing; recording then continues in a fresh buffer.
  call F05Mark("start_file")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_F05Tick==28 then
  if v=="d" then
   call F05Camera(1750.0,1800.0)
  else
   call F05Camera(1600.0,1024.0)
  endif
  call F05Mark("camera")
 elseif udg_F05Tick==30 then
  if v=="b" then
   loop
    exitwhen i>5
    call IssuePointOrder(udg_F05U[i],"move",1600.0,1024.0)
    set i=i+1
   endloop
   call F05Mark("independent_orders")
  else
   call F05Mark("click_due")
  endif
 elseif udg_F05Tick==248 and v=="d" then
  call F05Camera(1792.0,384.0)
  call F05Mark("camera_ring")
 elseif udg_F05Tick==250 and v=="d" then
  call F05Mark("click_ring_due")
 endif
 if (v!="d" and udg_F05Tick==300) or udg_F05Tick==500 then
  loop
   exitwhen i>5
   call IssueImmediateOrder(udg_F05U[i],"stop")
   call PauseUnit(udg_F05U[i],true)
   set i=i+1
  endloop
  call F05Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_F05Timer)
  return
 endif
 call F05Mark("sample")
endfunction

function PathProbeInit takes nothing returns nothing
 local integer i=0
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call EnableUserControl(true)
 call ShowInterface(true,0.0)
 call ClearSelection()
 if "@VARIANT@"=="d" then
  set udg_F05Gate=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',512.0,1700.0,0.0)
  call WaygateSetDestination(udg_F05Gate,1600.0,1750.0)
  call WaygateActivate(udg_F05Gate,true)
 endif
 loop
  exitwhen i==6
  set udg_F05U[i]=CreateUnit(GetLocalPlayer(),'hF00'+ModuloInteger(i,4),320.0,832.0+I2R(i)*64.0,0.0)
  if i<3 then
   call SetUnitMoveSpeed(udg_F05U[i],100.0)
  else
   call SetUnitMoveSpeed(udg_F05U[i],350.0)
  endif
  call SetUnitAcquireRange(udg_F05U[i],0.0)
  call SelectUnit(udg_F05U[i],true)
  set i=i+1
 endloop
 call SetCameraPosition(512.0,512.0)
 call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
 call F05Mark("start")
 set udg_F05Timer=CreateTimer()
 call TimerStart(udg_F05Timer,0.1,true,function F05Tick)
endfunction
