globals
 unit udg_T215Mover=null
 unit udg_T215Target=null
 timer udg_T215Timer=null
 integer udg_T215Tick=0
endglobals

function T215Bool takes boolean value returns string
 if value then
  return "true"
 endif
 return "false"
endfunction

function T215Mark takes string label returns nothing
 call Preload("T215 tick="+I2S(udg_T215Tick)+" label="+label+" fog="+T215Bool(IsFogEnabled())+" mask="+T215Bool(IsFogMaskEnabled())+" order="+I2S(GetUnitCurrentOrder(udg_T215Mover)))
endfunction

function T215Tick takes nothing returns nothing
 local boolean accepted=false
 set udg_T215Tick=udg_T215Tick+1
 if udg_T215Tick==1 then
  call T215Mark("start_file")
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_T215Tick==20 or udg_T215Tick==60 then
  call T215Mark("before_iseedeadpeople")
  call Cheat("iseedeadpeople")
  call T215Mark("after_iseedeadpeople")
 elseif udg_T215Tick==40 then
  call FogEnable(true)
  call FogMaskEnable(true)
  call T215Mark("fog_reenabled")
 elseif udg_T215Tick==42 or udg_T215Tick==55 then
  call UnitAddAbility(udg_T215Target,'Apiv')
  call T215Mark("invisible")
 elseif udg_T215Tick==46 or udg_T215Tick==57 then
  call UnitRemoveAbility(udg_T215Target,'Apiv')
  call T215Mark("visible")
 elseif udg_T215Tick==45 then
  set accepted=IssueTargetOrder(udg_T215Mover,"smart",udg_T215Target)
  call T215Mark("invisible_accepted_"+T215Bool(accepted))
 elseif udg_T215Tick==80 or udg_T215Tick==100 then
  call T215Mark("before_Telemetry")
  call Cheat("Telemetry")
  call T215Mark("after_Telemetry")
 elseif ModuloInteger(udg_T215Tick,20)==10 then
  set accepted=IssueTargetOrder(udg_T215Mover,"smart",udg_T215Target)
  call T215Mark("accepted_"+T215Bool(accepted))
  if udg_T215Tick!=50 then
   call IssueImmediateOrder(udg_T215Mover,"stop")
  endif
 elseif udg_T215Tick==120 then
  call T215Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_T215Timer)
 endif
endfunction

function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(true)
 call FogMaskEnable(true)
 call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,true)
 set udg_T215Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,1024.0,0.0)
 set udg_T215Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1760.0,1024.0,0.0)
 call SetUnitAcquireRange(udg_T215Mover,0.0)
 call SetUnitAcquireRange(udg_T215Target,0.0)
 call FogModifierStart(CreateFogModifierRadius(GetLocalPlayer(),FOG_OF_WAR_FOGGED,1760.0,1024.0,500.0,false,false))
 call SetCameraPosition(1024.0,1024.0)
 call T215Mark("start")
 set udg_T215Timer=CreateTimer()
 call TimerStart(udg_T215Timer,0.1,true,function T215Tick)
endfunction
