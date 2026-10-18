globals
 unit udg_G338Actor=null
 unit udg_G338Old=null
 unit udg_G338New=null
 unit udg_G338Caster=null
 integer udg_G338Tick=0
 integer udg_G338Scene=-1
 integer udg_G338Local=0
 integer array udg_G338Types
 timer udg_G338Timer=null
endglobals
function G338Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_G338New) then
  set paused=1
 endif
 call Preload("R367 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" sleep="+I2S(GetUnitAbilityLevel(udg_G338New,'BUsl'))+" pause="+I2S(GetUnitAbilityLevel(udg_G338New,'BUsp'))+" order="+I2S(GetUnitCurrentOrder(udg_G338New))+" paused="+I2S(paused)+" result="+I2S(udg_G338Types[100])+" skill="+I2S(GetUnitAbilityLevel(udg_G338Caster,'A36r')))
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==4 then
   call G338Mark("complete")
   call PreloadGenEnd("refresh367.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  set udg_G338Types[100]=0
  set udg_G338New=CreateUnit(Player(2),'hfoo',1024.,512.,0.)
  set udg_G338Caster=CreateUnit(Player(0),'Udre',1280.,512.,180.)
  set udg_G338Old=CreateUnit(Player(0),'Udre',1280.,768.,180.)
  call SetUnitInvulnerable(udg_G338Caster,true)
  call SetUnitInvulnerable(udg_G338Old,true)
  call UnitAddAbility(udg_G338Caster,'A36r')
  call UnitAddAbility(udg_G338Old,'A36r')
  call SetUnitAbilityLevel(udg_G338Caster,'A36r',1)
  call SetUnitAbilityLevel(udg_G338Old,'A36r',1)
  call SetUnitState(udg_G338Caster,UNIT_STATE_MANA,255.)
  call SetUnitState(udg_G338Old,UNIT_STATE_MANA,255.)
  call IssuePointOrder(udg_G338New,"move",1024.,1792.)
  if udg_G338Scene==3 then
   call PauseUnit(udg_G338New,true)
  endif
  call G338Mark("setup")
 elseif udg_G338Local==3 then
  call G338Mark("cast-before")
  set udg_G338Types[100]=0
  if IssueTargetOrder(udg_G338Caster,"sleep",udg_G338New) then
   set udg_G338Types[100]=1
  endif
  call G338Mark("cast-after")
 elseif udg_G338Local==10 and udg_G338Scene==1 then
  call G338Mark("victim-pause-before")
  call PauseUnit(udg_G338New,true)
  call G338Mark("victim-pause-after")
 elseif udg_G338Local==12 then
  call PauseUnit(udg_G338Caster,true)
  call G338Mark("source-paused")
 elseif udg_G338Local==15 and udg_G338Scene==3 then
  call G338Mark("guard-remove-before")
  set udg_G338Types[100]=0
  if UnitRemoveAbility(udg_G338New,'BUsp') then
   set udg_G338Types[100]=1
  endif
  call G338Mark("guard-remove-after")
 elseif udg_G338Local==20 then
  call G338Mark("refresh-before")
  set udg_G338Types[100]=0
  if IssueTargetOrder(udg_G338Old,"sleep",udg_G338New) then
   set udg_G338Types[100]=1
  endif
  call G338Mark("refresh-after")
 elseif udg_G338Local==25 and udg_G338Scene==2 then
  call G338Mark("victim-pause-before")
  call PauseUnit(udg_G338New,true)
  call G338Mark("victim-pause-after")
 elseif udg_G338Local==30 then
  call PauseUnit(udg_G338Old,true)
  call G338Mark("second-source-paused")
 elseif udg_G338Local==60 then
  call G338Mark("victim-unpause-before")
  call PauseUnit(udg_G338New,false)
  call G338Mark("victim-unpause-after")
 elseif udg_G338Local==100 then
  call RemoveUnit(udg_G338New)
  call RemoveUnit(udg_G338Caster)
  call RemoveUnit(udg_G338Old)
  set udg_G338New=null
  set udg_G338Caster=null
  set udg_G338Old=null
  call G338Mark("cleanup")
  set udg_G338Local=0
 elseif udg_G338Local>=35 and ModuloInteger(udg_G338Local,5)==0 then
  call G338Mark("continuation")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetRandomSeed(1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
