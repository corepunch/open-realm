globals
 unit udg_T218Mover=null
 unit udg_T218Target=null
 unit udg_T218Detector=null
 timer udg_T218Timer=null
 fogmodifier udg_T218Fog=null
 integer udg_T218Tick=0
 integer udg_T218Scene=0
endglobals
function T218Bool takes boolean value returns string
 if value then
  return "true"
 endif
 return "false"
endfunction
function T218Mark takes string label returns nothing
 call Preload("T218 tick="+I2S(udg_T218Tick)+" scene="+I2S(udg_T218Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T218Mover))+" visible="+T218Bool(IsUnitVisible(udg_T218Target,GetLocalPlayer()))+" invisible="+T218Bool(IsUnitInvisible(udg_T218Target,GetLocalPlayer()))+" detected="+T218Bool(IsUnitDetected(udg_T218Target,GetLocalPlayer()))+" detected1="+T218Bool(IsUnitDetected(udg_T218Target,Player(1)))+" detectorlevel="+I2S(GetUnitAbilityLevel(udg_T218Detector,'Atru'))+" x="+R2S(GetUnitX(udg_T218Mover))+" y="+R2S(GetUnitY(udg_T218Mover)))
endfunction
function T218Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T218Tick,55)
 local player owner=Player(PLAYER_NEUTRAL_AGGRESSIVE)
 local player detectorOwner=GetLocalPlayer()
 local boolean accepted=false
 if t==0 then
  call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_AGGRESSIVE),ALLIANCE_PASSIVE,false)
  call SetPlayerAlliance(Player(PLAYER_NEUTRAL_AGGRESSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,false)
  if udg_T218Scene==1 then
   set owner=Player(PLAYER_NEUTRAL_PASSIVE)
  elseif udg_T218Scene==6 then
   set owner=GetLocalPlayer()
  endif
  if udg_T218Scene==2 or udg_T218Scene==3 or udg_T218Scene==5 then
   set detectorOwner=Player(1)
  endif
  call SetPlayerAlliance(detectorOwner,owner,ALLIANCE_PASSIVE,udg_T218Scene==1 or udg_T218Scene==6)
  call SetPlayerAlliance(owner,detectorOwner,ALLIANCE_PASSIVE,udg_T218Scene==1 or udg_T218Scene==6)
  set udg_T218Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,1024.0,0.0)
  set udg_T218Target=CreateUnit(owner,'hF00',1760.0,1024.0,0.0)
  call SetUnitAcquireRange(udg_T218Mover,0.0)
  call SetUnitAcquireRange(udg_T218Target,0.0)
  call UnitAddAbility(udg_T218Target,'Apiv')
  if udg_T218Scene==4 or udg_T218Scene==5 then
   call FogEnable(true)
   call FogMaskEnable(true)
   set udg_T218Fog=CreateFogModifierRadius(GetLocalPlayer(),FOG_OF_WAR_FOGGED,1760.0,1024.0,768.0,false,true)
   call FogModifierStart(udg_T218Fog)
  endif
  if udg_T218Scene==2 or udg_T218Scene==5 then
   call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,true)
  endif
  call T218Mark("begin")
 elseif t==5 then
  set accepted=IssueTargetOrder(udg_T218Mover,"move",udg_T218Target)
  if accepted then
   call T218Mark("before_accepted")
  else
   call T218Mark("before_rejected")
  endif
 elseif t==10 then
  if udg_T218Scene<6 then
   if udg_T218Scene==2 or udg_T218Scene==3 or udg_T218Scene==5 then
    set detectorOwner=Player(1)
   endif
   set udg_T218Detector=CreateUnit(detectorOwner,'ushd',1696.0,1024.0,0.0)
   call SetUnitAcquireRange(udg_T218Detector,0.0)
   call UnitAddAbility(udg_T218Detector,'Atru')
   call SetUnitX(udg_T218Detector,1680.0)
  elseif udg_T218Scene==7 then
   call UnitShareVision(udg_T218Target,GetLocalPlayer(),true)
  endif
  call T218Mark("detector_added")
 elseif t==15 then
  set accepted=IssueTargetOrder(udg_T218Mover,"move",udg_T218Target)
  if accepted then
   call T218Mark("detected_accepted")
  else
   call T218Mark("detected_rejected")
  endif
 elseif t==25 then
  if udg_T218Detector!=null then
   call UnitRemoveAbility(udg_T218Detector,'Atru')
  elseif udg_T218Scene==7 then
   call UnitShareVision(udg_T218Target,GetLocalPlayer(),false)
  endif
  call T218Mark("detector_removed")
 elseif t==30 then
  call T218Mark("after_removal")
 elseif t==45 then
  call T218Mark("settled")
 elseif t==50 then
  call IssueImmediateOrder(udg_T218Mover,"stop")
  call RemoveUnit(udg_T218Mover)
  call RemoveUnit(udg_T218Target)
  if udg_T218Detector!=null then
   call RemoveUnit(udg_T218Detector)
   set udg_T218Detector=null
  endif
  if udg_T218Fog!=null then
   call DestroyFogModifier(udg_T218Fog)
   set udg_T218Fog=null
  endif
  call SetPlayerAlliance(Player(1),GetLocalPlayer(),ALLIANCE_SHARED_VISION,false)
  call FogEnable(false)
  call FogMaskEnable(false)
 elseif t==54 then
  if udg_T218Scene==7 then
   call T218Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T218Timer)
  endif
  set udg_T218Scene=udg_T218Scene+1
 endif
 set udg_T218Tick=udg_T218Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1028)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,true)
 set udg_T218Timer=CreateTimer()
 call TimerStart(udg_T218Timer,0.1,true,function T218Tick)
 call Preload("T218 tick=0 scene=0 label=start order=0")
endfunction
