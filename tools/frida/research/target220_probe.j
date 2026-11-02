globals
 unit udg_T220Mover=null
 unit udg_T220Target=null
 timer udg_T220Timer=null
 fogmodifier udg_T220Fog=null
 integer udg_T220Tick=0
 integer udg_T220Scene=0
endglobals
function T220Mark takes string label returns nothing
 call Preload("T220 tick="+I2S(udg_T220Tick)+" scene="+I2S(udg_T220Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T220Mover))+" x="+R2S(GetUnitX(udg_T220Mover))+" y="+R2S(GetUnitY(udg_T220Mover)))
endfunction
function T220Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T220Tick,240)
 if udg_T220Tick==1 then
  call T220Mark("start_file")
  call PreloadGenEnd("rs-t220-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if t==0 then
  call FogEnable(false)
  call FogMaskEnable(false)
  set udg_T220Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,320.0,90.0)
  set udg_T220Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1600.0,1024.0,180.0)
  call SetUnitAcquireRange(udg_T220Mover,0.0)
  call SetUnitAcquireRange(udg_T220Target,0.0)
  call SetUnitMoveSpeed(udg_T220Mover,100.0)
  call ClearSelection()
  call SelectUnit(udg_T220Mover,true)
  call SetCameraPosition(1493.2267,844.7426)
  call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
  call IssuePointOrder(udg_T220Mover,"move",320.0,1856.0)
  call T220Mark("begin")
 elseif t==8 then
  call SetCameraPosition(1493.2267,844.7426)
  call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE,1800.0,0.0)
 elseif t==10 then
  call T220Mark("click_due")
 elseif t==75 and udg_T220Scene==5 then
  call SetUnitPosition(udg_T220Target,1840.0,1024.0)
  call T220Mark("target_moved")
 elseif t==80 then
  if udg_T220Scene==1 then
   call UnitAddAbility(udg_T220Target,'Apiv')
  elseif udg_T220Scene==2 or udg_T220Scene==5 then
   call RemoveUnit(udg_T220Target)
  elseif udg_T220Scene==3 then
   call KillUnit(udg_T220Target)
  elseif udg_T220Scene==4 then
   call FogEnable(true)
   call FogMaskEnable(true)
   set udg_T220Fog=CreateFogModifierRadius(GetLocalPlayer(),FOG_OF_WAR_FOGGED,1600.0,1024.0,384.0,false,true)
   call FogModifierStart(udg_T220Fog)
  endif
  call T220Mark("target_changed")
 elseif t==225 then
  call T220Mark("settled")
 elseif t==230 then
  call IssueImmediateOrder(udg_T220Mover,"stop")
  call RemoveUnit(udg_T220Mover)
  if udg_T220Scene!=2 and udg_T220Scene!=5 then
   call RemoveUnit(udg_T220Target)
  endif
  if udg_T220Fog!=null then
   call DestroyFogModifier(udg_T220Fog)
   set udg_T220Fog=null
  endif
 elseif t==239 then
  if udg_T220Scene==6 then
   call T220Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T220Timer)
  endif
  set udg_T220Scene=udg_T220Scene+1
 endif
 if t<230 then
  call T220Mark("sample")
 endif
 set udg_T220Tick=udg_T220Tick+1
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
 call T220Mark("start")
 set udg_T220Timer=CreateTimer()
 call TimerStart(udg_T220Timer,0.1,true,function T220Tick)
endfunction
