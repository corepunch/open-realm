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
 call Preload("F367 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" sleep="+I2S(GetUnitAbilityLevel(udg_G338New,'BUsl'))+" pause="+I2S(GetUnitAbilityLevel(udg_G338New,'BUsp'))+" stun="+I2S(GetUnitAbilityLevel(udg_G338New,'BUst'))+" generic="+I2S(GetUnitAbilityLevel(udg_G338New,'BSTN'))+" pausedbuff="+I2S(GetUnitAbilityLevel(udg_G338New,'BPSE'))+" order="+I2S(GetUnitCurrentOrder(udg_G338New))+" life="+R2SW(GetWidgetLife(udg_G338New),1,4)+" result="+I2S(udg_G338Types[100]))
endfunction
function G338Tick takes nothing returns nothing
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==8 then
   call G338Mark("complete")
   call PreloadGenEnd("group367-factory.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  set udg_G338Types[100]=0
  set udg_G338New=CreateUnit(Player(2),'hfoo',1024.,512.,0.)
  set udg_G338Caster=CreateUnit(Player(0),'Udre',1280.,512.,180.)
  call SetUnitInvulnerable(udg_G338Caster,true)
  call UnitAddAbility(udg_G338Caster,udg_G338Types[udg_G338Scene])
  call SetUnitAbilityLevel(udg_G338Caster,udg_G338Types[udg_G338Scene],1)
  call SetUnitState(udg_G338Caster,UNIT_STATE_MANA,255.)
  call IssuePointOrder(udg_G338New,"move",1024.,1792.)
  call G338Mark("setup")
 elseif udg_G338Local==3 then
  call G338Mark("cast-before")
  if udg_G338Scene<4 then
   call IssueTargetOrder(udg_G338Caster,"thunderbolt",udg_G338New)
  else
   call IssueTargetOrder(udg_G338Caster,"sleep",udg_G338New)
  endif
  call G338Mark("cast-after")
 elseif udg_G338Local==12 or udg_G338Local==25 or udg_G338Local==35 then
  call G338Mark("constructed")
 elseif udg_G338Local==40 then
  call RemoveUnit(udg_G338New)
  call RemoveUnit(udg_G338Caster)
  set udg_G338New=null
  set udg_G338Caster=null
  call G338Mark("cleanup")
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetRandomSeed(1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 set udg_G338Types[0]='AHtb'
 set udg_G338Types[1]='A36a'
 set udg_G338Types[2]='A36b'
 set udg_G338Types[3]='A36c'
 set udg_G338Types[4]='AUsl'
 set udg_G338Types[5]='A36d'
 set udg_G338Types[6]='A36e'
 set udg_G338Types[7]='A36f'
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
