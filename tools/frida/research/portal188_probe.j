globals
 timer udg_P188Timer=null
 integer udg_P188Tick=0
 unit udg_P188Mover=null
 unit udg_P188Gate=null
 unit udg_P188Blocker=null
endglobals
// Public point Move across the wall through an enabled Way Gate.
function P188Mark takes string label returns nothing
 call Preload("P188 tick="+I2S(udg_P188Tick)+" s=0 label="+label+" x="+R2S(GetUnitX(udg_P188Mover))+" y="+R2S(GetUnitY(udg_P188Mover))+" order="+I2S(GetUnitCurrentOrder(udg_P188Mover)))
endfunction
function P188Tick takes nothing returns nothing
 set udg_P188Tick=udg_P188Tick+1
 if udg_P188Tick==20 then
  call P188Mark("begin-setup")
  set udg_P188Gate=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'nwgt',640.0,288.0,0.0)
  call WaygateSetDestination(udg_P188Gate,1600.0,512.0)
  call WaygateActivate(udg_P188Gate,true)
  set udg_P188Mover=CreateUnit(Player(0),'hfoo',384.0,288.0,0.0)
  call SetUnitAcquireRange(udg_P188Mover,0.0)
  // Occupied exit admits a neighbouring point rather than overlapping.
  set udg_P188Blocker=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hfoo',1600.0,512.0,0.0)
 endif
 if udg_P188Tick==25 then
  call IssuePointOrder(udg_P188Mover,"move",1568.0,1312.0)
  call P188Mark("move")
 endif
 if udg_P188Tick>=20 and udg_P188Tick<180 then
  call P188Mark("sample")
 endif
 if udg_P188Tick==180 then
  call IssueImmediateOrder(udg_P188Mover,"stop")
  call P188Mark("stop")
 endif
 if udg_P188Tick==185 then
  call P188Mark("begin-cleanup")
  call RemoveUnit(udg_P188Mover)
  call RemoveUnit(udg_P188Gate)
  call RemoveUnit(udg_P188Blocker)
  call P188Mark("end-cleanup")
 endif
 if udg_P188Tick==190 then
  call P188Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_P188Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set udg_P188Timer=CreateTimer()
 call TimerStart(udg_P188Timer,0.1,true,function P188Tick)
endfunction
