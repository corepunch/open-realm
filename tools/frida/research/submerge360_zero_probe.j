globals
 timer udg_Z360Timer=null
 unit udg_Z360Unit=null
 integer udg_Z360Tick=0
 integer udg_Z360Scene=0
endglobals
function Z360Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_Z360Unit) then
  set paused=1
 endif
 call Preload("Z360 tick="+I2S(udg_Z360Tick)+" scene="+I2S(udg_Z360Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_Z360Unit))+" order="+I2S(GetUnitCurrentOrder(udg_Z360Unit))+" paused="+I2S(paused))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function Z360Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_Z360Tick,80)
 local integer form=udg_Z360Scene/3
 local integer action=ModuloInteger(udg_Z360Scene,3)
 local boolean ok=false
 if age==0 then
  if form==0 then
   set udg_Z360Unit=CreateUnit(Player(0),'nmyr',640,1024,0)
   call UnitAddAbility(udg_Z360Unit,'Asb1')
   call UnitMakeAbilityPermanent(udg_Z360Unit,true,'Asb1')
  elseif form==1 then
   set udg_Z360Unit=CreateUnit(Player(0),'nnrg',640,1024,0)
   call UnitAddAbility(udg_Z360Unit,'Asb2')
   call UnitMakeAbilityPermanent(udg_Z360Unit,true,'Asb2')
  else
   set udg_Z360Unit=CreateUnit(Player(0),'nsnp',640,1024,0)
   call UnitAddAbility(udg_Z360Unit,'Asb3')
   call UnitMakeAbilityPermanent(udg_Z360Unit,true,'Asb3')
  endif
  call SetUnitAcquireRange(udg_Z360Unit,0)
  call SetUnitState(udg_Z360Unit,UNIT_STATE_MANA,1000)
  call Z360Mark("created")
 elseif age==2 or age==42 then
  if age==2 then
   set ok=IssueImmediateOrder(udg_Z360Unit,"submerge")
  else
   set ok=IssueImmediateOrder(udg_Z360Unit,"unsubmerge")
  endif
  if ok then
   call Z360Mark("cast-accepted")
  else
   call Z360Mark("cast-rejected")
  endif
 elseif age==3 or age==43 then
  if action==1 then
   set ok=IssueImmediateOrder(udg_Z360Unit,"stop")
   if ok then
    call Z360Mark("stop-accepted")
   else
    call Z360Mark("stop-rejected")
   endif
  elseif action==2 then
   call PauseUnit(udg_Z360Unit,true)
   call Z360Mark("pause")
  elseif action==3 then
   call KillUnit(udg_Z360Unit)
   call Z360Mark("kill")
  endif
 elseif (age==25 or age==65) and action==2 then
  call PauseUnit(udg_Z360Unit,false)
  call Z360Mark("unpause")
 elseif age==77 then
  call RemoveUnit(udg_Z360Unit)
  call Z360Mark("cleanup")
 elseif age==79 then
  if udg_Z360Scene==0 then
   call Z360Mark("complete")
   call PauseTimer(udg_Z360Timer)
  endif
  set udg_Z360Scene=udg_Z360Scene+1
 endif
 if age<77 then
  call Z360Mark("sample")
 endif
 set udg_Z360Tick=udg_Z360Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
 call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_CAP,300)
 call SetPlayerTechResearched(Player(0),'Ruba',1)
 call SetRandomSeed(1360)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call SetPlayerTechResearched(Player(0),'Redc',3)
 call SetPlayerTechResearched(Player(0),'Redt',3)
 call Z360Mark("start")
 set udg_Z360Timer=CreateTimer()
 call TimerStart(udg_Z360Timer,0.1,true,function Z360Tick)
endfunction
