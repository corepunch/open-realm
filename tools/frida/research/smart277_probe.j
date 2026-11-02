globals
 unit udg_S277Mover=null
 unit udg_S277Target=null
 timer udg_S277Timer=null
 fogmodifier udg_S277Fog=null
 integer udg_S277Tick=0
 integer udg_S277Scene=2
endglobals
function S277Mark takes string label returns nothing
 call Preload("S277 tick="+I2S(udg_S277Tick)+" scene="+I2S(udg_S277Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_S277Mover))+" x="+R2S(GetUnitX(udg_S277Mover))+" y="+R2S(GetUnitY(udg_S277Mover)))
endfunction
function S277Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_S277Tick,240)
 if udg_S277Tick==1 then
  call S277Mark("start_file")
  call PreloadGenEnd("rs-s277-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if t==0 then
  call FogEnable(false)
  call FogMaskEnable(false)
  set udg_S277Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,320.0,90.0)
  set udg_S277Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1600.0,1024.0,180.0)
  call SetUnitAcquireRange(udg_S277Mover,0.0)
  call SetUnitAcquireRange(udg_S277Target,0.0)
  call SetUnitMoveSpeed(udg_S277Mover,100.0)
  call ClearSelection()
  call SelectUnit(udg_S277Mover,true)
  call SetCameraPosition(1493.2267,844.7426)
  call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
  call IssuePointOrder(udg_S277Mover,"move",320.0,1856.0)
  call S277Mark("begin")
 elseif t==8 then
  call SetCameraPosition(1493.2267,844.7426)
  call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
 elseif t==10 then
  call S277Mark("click_due")
 elseif t==75 and udg_S277Scene==5 then
  call SetUnitPosition(udg_S277Target,1840.0,1024.0)
  call S277Mark("target_moved")
 elseif t==80 then
  if udg_S277Scene==1 then
   call UnitAddAbility(udg_S277Target,'Apiv')
  elseif udg_S277Scene==2 or udg_S277Scene==5 then
   call RemoveUnit(udg_S277Target)
  elseif udg_S277Scene==3 then
   call KillUnit(udg_S277Target)
  elseif udg_S277Scene==4 then
   call FogEnable(true)
   call FogMaskEnable(true)
   set udg_S277Fog=CreateFogModifierRadius(GetLocalPlayer(),FOG_OF_WAR_FOGGED,1600.0,1024.0,384.0,false,true)
   call FogModifierStart(udg_S277Fog)
  endif
  call S277Mark("target_changed")
  call S277Mark("after_remove")
 elseif t==225 then
  call S277Mark("settled")
 elseif t==230 then
  call IssueImmediateOrder(udg_S277Mover,"stop")
  call RemoveUnit(udg_S277Mover)
  if udg_S277Scene!=2 and udg_S277Scene!=5 then
   call RemoveUnit(udg_S277Target)
  endif
  if udg_S277Fog!=null then
   call DestroyFogModifier(udg_S277Fog)
   set udg_S277Fog=null
  endif
 elseif t==239 then
  if udg_S277Scene==2 then
   call S277Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_S277Timer)
  endif
  set udg_S277Scene=udg_S277Scene+1
 endif
 if t<230 then
  call S277Mark("sample")
 endif
 set udg_S277Tick=udg_S277Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1030)
 call SetPlayerController(GetLocalPlayer(),MAP_CONTROL_USER)
 call EnableUserControl(true)
 call ShowInterface(true,0.0)
 call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,true)
 call S277Mark("start")
 set udg_S277Timer=CreateTimer()
 call TimerStart(udg_S277Timer,0.1,true,function S277Tick)
endfunction
