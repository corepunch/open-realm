globals
 unit udg_T217Mover=null
 unit udg_T217Target=null
 timer udg_T217Timer=null
 fogmodifier udg_T217Fog=null
 integer udg_T217Tick=0
 integer udg_T217Scene=0
endglobals
function T217Bool takes boolean value returns string
 if value then
  return "true"
 endif
 return "false"
endfunction
function T217Mark takes string label returns nothing
 call Preload("T217 tick="+I2S(udg_T217Tick)+" scene="+I2S(udg_T217Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T217Mover))+" visible="+T217Bool(IsUnitVisible(udg_T217Target,GetLocalPlayer()))+" invisible="+T217Bool(IsUnitInvisible(udg_T217Target,GetLocalPlayer()))+" x="+R2S(GetUnitX(udg_T217Mover))+" y="+R2S(GetUnitY(udg_T217Mover)))
endfunction
function T217Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T217Tick,55)
 local boolean accepted=false
 if t==0 then
  set udg_T217Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,1024.0,0.0)
  set udg_T217Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1760.0,1024.0,0.0)
  call SetUnitAcquireRange(udg_T217Mover,0.0)
  call SetUnitAcquireRange(udg_T217Target,0.0)
  call UnitAddAbility(udg_T217Target,'Apiv')
  if ModuloInteger(udg_T217Scene,4)>=2 then
   call FogEnable(true)
   call FogMaskEnable(true)
   set udg_T217Fog=CreateFogModifierRadius(GetLocalPlayer(),FOG_OF_WAR_FOGGED,1760.0,1024.0,768.0,false,true)
   call FogModifierStart(udg_T217Fog)
  endif
  call T217Mark("begin")
 elseif t==5 then
  set accepted=IssueTargetOrder(udg_T217Mover,"smart",udg_T217Target)
  if accepted then
   call T217Mark("before_accepted")
  else
   call T217Mark("before_rejected")
  endif
 elseif t==10 then
  if ModuloInteger(udg_T217Scene,2)==0 then
   call UnitShareVision(udg_T217Target,GetLocalPlayer(),true)
  else
   if udg_T217Scene>=4 then
    call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,true)
   endif
   call UnitShareVision(udg_T217Target,Player(1),true)
   if udg_T217Scene<4 then
    call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,true)
   endif
  endif
  call T217Mark("shared")
 elseif t==15 then
  set accepted=IssueTargetOrder(udg_T217Mover,"smart",udg_T217Target)
  if accepted then
   call T217Mark("shared_accepted")
  else
   call T217Mark("shared_rejected")
  endif
 elseif t==20 and udg_T217Scene>=4 and ModuloInteger(udg_T217Scene,2)==1 then
  call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,false)
  call T217Mark("alliance_revoked")
 elseif t==22 and udg_T217Scene>=4 and ModuloInteger(udg_T217Scene,2)==1 then
  call UnitShareVision(udg_T217Target,Player(1),true)
  call T217Mark("share_refreshed")
 elseif t==25 then
  if ModuloInteger(udg_T217Scene,2)==0 then
   call UnitShareVision(udg_T217Target,GetLocalPlayer(),false)
  else
   call UnitShareVision(udg_T217Target,Player(1),false)
  endif
  call T217Mark("unshared")
 elseif t==30 then
  call T217Mark("after_unshare")
 elseif t==45 then
  call T217Mark("settled")
 elseif t==50 then
  call IssueImmediateOrder(udg_T217Mover,"stop")
  call RemoveUnit(udg_T217Mover)
  call RemoveUnit(udg_T217Target)
  if udg_T217Fog!=null then
   call DestroyFogModifier(udg_T217Fog)
   set udg_T217Fog=null
  endif
  call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,false)
  call FogEnable(false)
  call FogMaskEnable(false)
 elseif t==54 then
  if udg_T217Scene==7 then
   call T217Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T217Timer)
  endif
  set udg_T217Scene=udg_T217Scene+1
 endif
 set udg_T217Tick=udg_T217Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,true)
 set udg_T217Timer=CreateTimer()
 call TimerStart(udg_T217Timer,0.1,true,function T217Tick)
 call Preload("T217 tick=0 scene=0 label=start order=0")
endfunction
