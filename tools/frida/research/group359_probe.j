globals
 unit udg_G338Actor=null
 unit udg_G338Old=null
 unit udg_G338New=null
 unit udg_G338Caster=null
 unit array udg_G338Extra
 integer array udg_G338Types
 integer udg_G338Tick=0
 integer udg_G338Scene=-1
 integer udg_G338Local=0
 timer udg_G338Timer=null
endglobals
function G338Mark takes string label returns nothing
 call Preload("G359 tick="+I2S(udg_G338Tick)+" scene="+I2S(udg_G338Scene)+" label="+label+" actor="+I2S(GetUnitTypeId(udg_G338Actor))+" target="+I2S(GetUnitTypeId(udg_G338New))+" order="+I2S(GetUnitCurrentOrder(udg_G338Actor))+" targetOrder="+I2S(GetUnitCurrentOrder(udg_G338New))+" freeze="+I2S(GetUnitAbilityLevel(udg_G338New,'Bfre'))+" breath="+I2S(GetUnitAbilityLevel(udg_G338New,'Bfrz'))+" sleepStun="+I2S(GetUnitAbilityLevel(udg_G338New,'BUst'))+" sleep="+I2S(GetUnitAbilityLevel(udg_G338New,'BUsl')))
endfunction
function G338Setup takes nothing returns nothing
 local integer target='hfoo'
 local integer caster='hrif'
 if udg_G338Scene==4 or udg_G338Scene==5 or udg_G338Scene==10 then
  set target='hgtw'
  set caster='ufro'
 elseif (udg_G338Scene>=6 and udg_G338Scene<=8) or udg_G338Scene==11 then
  set caster='Udre'
 endif
 set udg_G338New=CreateUnit(Player(2),target,1024.,512.,180.)
 set udg_G338Caster=CreateUnit(Player(0),caster,1280.,512.,180.)
 call SetUnitInvulnerable(udg_G338Caster,true)
 call IssueImmediateOrder(udg_G338New,"holdposition")
 if (udg_G338Scene>=1 and udg_G338Scene<=3) or udg_G338Scene==9 then
  call UnitAddAbility(udg_G338Caster,'AIzb')
  call SetUnitAbilityLevel(udg_G338Caster,'AIzb',1)
  call IssueTargetOrder(udg_G338Caster,"attack",udg_G338New)
 elseif udg_G338Scene==4 or udg_G338Scene==5 or udg_G338Scene==10 then
  call UnitAddAbility(udg_G338Caster,'Afrz')
  call SetPlayerTechResearched(Player(0),'Rufb',1)
  call SetUnitAbilityLevel(udg_G338Caster,'Afrz',1)
  call IssueTargetOrder(udg_G338Caster,"attack",udg_G338New)
 elseif (udg_G338Scene>=6 and udg_G338Scene<=8) or udg_G338Scene==11 then
  call UnitAddAbility(udg_G338Caster,'AUsl')
  call SetUnitAbilityLevel(udg_G338Caster,'AUsl',1)
  call SetUnitState(udg_G338Caster,UNIT_STATE_MANA,255.)
  call IssueTargetOrder(udg_G338Caster,"sleep",udg_G338New)
 endif
 call G338Mark("setup")
endfunction
function G338Tick takes nothing returns nothing
 local integer availability=14
 set udg_G338Tick=udg_G338Tick+1
 set udg_G338Local=udg_G338Local+1
 if udg_G338Scene==7 then
  set availability=26
 endif
 if udg_G338Local==1 then
  set udg_G338Scene=udg_G338Scene+1
  if udg_G338Scene==12 then
   call G338Mark("complete")
   call PreloadGenEnd("group359.txt")
   call PauseTimer(udg_G338Timer)
   return
  endif
  call G338Setup()
 elseif udg_G338Local==12 then
  call G338Mark("caster-before")
  call RemoveUnit(udg_G338Caster)
  set udg_G338Caster=null
  call G338Mark("caster-after")
 elseif udg_G338Local==13 then
  set udg_G338Actor=CreateUnit(Player(2),'hrif',512.,512.,0.)
  set udg_G338Old=CreateUnit(Player(1),'hfoo',576.,512.,180.)
  call SetUnitInvulnerable(udg_G338Actor,true)
  call SetUnitAcquireRange(udg_G338Actor,1100.)
  call IssueImmediateOrder(udg_G338Old,"holdposition")
  call IssuePointOrder(udg_G338Actor,"attack",1864.,512.)
 elseif udg_G338Local==availability then
  if udg_G338Scene>=9 then
   call UnitRemoveAbility(udg_G338New,'Aatk')
   call UnitAddAbility(udg_G338New,'Aatk')
  endif
  if udg_G338Scene==2 or udg_G338Scene==5 or udg_G338Scene==8 then
   call UnitRemoveBuffs(udg_G338New,false,true)
  elseif udg_G338Scene==3 then
   call UnitRemoveAbility(udg_G338New,'Aatk')
  endif
  call G338Mark("availability-before")
  call SetUnitOwner(udg_G338New,Player(1),false)
  call G338Mark("availability-after")
 elseif udg_G338Local==availability+2 then
  call G338Mark("inverse-before")
  call UnitRemoveBuffs(udg_G338New,false,true)
  call SetUnitOwner(udg_G338New,Player(3),false)
  call G338Mark("inverse-after")
 elseif udg_G338Local==45 then
  call G338Mark("end-before")
  call RemoveUnit(udg_G338Actor)
  call RemoveUnit(udg_G338Old)
  call RemoveUnit(udg_G338New)
  set udg_G338Actor=null
  set udg_G338Old=null
  set udg_G338New=null
  call G338Mark("end-after")
  set udg_G338Local=0
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call SetRandomSeed(1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(2),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(2),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(2),Player(3),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(3),Player(2),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 call G338Mark("start")
 set udg_G338Timer=CreateTimer()
 call TimerStart(udg_G338Timer,.1,true,function G338Tick)
endfunction
